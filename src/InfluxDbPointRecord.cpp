#include "InfluxDbPointRecord.h"

#include <boost/asio.hpp>
#include <boost/foreach.hpp>
#include <curl/curl.h>
#include <map>
#include <boost/regex.hpp>
#include <boost/lexical_cast.hpp>



#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

//#include <boost/timer/timer.hpp>

using namespace std;
using namespace RTX;
using boost::asio::ip::tcp;

#define HTTP_OK 200



InfluxDbPointRecord::InfluxDbPointRecord() {
  _connected = false;
  _range = make_pair(0,0);
  
  host = "*HOST*";
  user = "*USER*";
  pass = "*PASS*";
  port = 8086;
  db = "*DB*";
  
}

void InfluxDbPointRecord::dbConnect() throw(RtxException) {
  
  stringstream q;
  q << "/ping?u=" << this->user << "&p=" << this->pass;
  
  JsonDocPtr doc = this->jsonFromPath(q.str());
  if (!doc) {
    _connected = false;
    cerr << "could not connect" << endl;
    errorMessage = "Could Not Connect";
  }
  else {
    _connected = true;
    errorMessage = "OK";
  }
  
}


string InfluxDbPointRecord::connectionString() {
  
  stringstream ss;
  ss << "host=" << this->host << "&port=" << this->port << "&db=" << this->db << "&u=" << this->user << "&p=" << this->pass;
  
  return ss.str();
}

void InfluxDbPointRecord::setConnectionString(const std::string &str) {
  
  // split the tokenized string. we're expecting something like "host=127.0.0.1&port=4242;"
  std::map<std::string, std::string> kvPairs;
  {
    boost::regex kvReg("([^=]+)=([^&]+)&?"); // key - value pair
    boost::sregex_iterator it(str.begin(), str.end(), kvReg), end;
    for ( ; it != end; ++it) {
      kvPairs[(*it)[1]] = (*it)[2];
    }
  }
  
  std::map<std::string, std::string>::iterator notfound = kvPairs.end();
  
  if (kvPairs.find("host") != notfound) {
    this->host = kvPairs["host"];
  }
  if (kvPairs.find("port") != notfound) {
    int intPort = boost::lexical_cast<int>(kvPairs["port"]);
    this->port = intPort;
  }
  if (kvPairs.find("db") != notfound) {
    this->db = kvPairs["db"];
  }
  if (kvPairs.find("u") != notfound) {
    this->user = kvPairs["u"];
  }
  if (kvPairs.find("p") != notfound) {
    this->pass = kvPairs["p"];
  }
  
  
  return;
}


bool InfluxDbPointRecord::insertIdentifierAndUnits(const std::string &id, RTX::Units units) {

  
  MetricInfo m = this->metricInfoFromName(id);
  
  /*
   NAMES MUST HAVE A UNITS STRING:
   measurement,units=units_string
   measurement,key1=value1,key2=value2,units=units_string
   */
  if ( m.tags.find("units") == m.tags.end() ) {
    return false;
  }
  else if ( !RTX_STRINGS_ARE_EQUAL(m.tags["units"], units.unitString()) ) {
    // units don't match. reject.
    return false;
  }
  
  
  
  
  
  /*
  
   we don't require validation here - just assume that everything will write ok
   
   
   
   
   
  
  // placeholder in db. insert a point to create the ts, then drop the points (but not the ts)
  
  bool alreadyInIndex = false;
  
  vector<nameUnitsPair> existing = this->identifiersAndUnits();
  BOOST_FOREACH( const nameUnitsPair p, existing) {
    string name = p.first;
    Units units = p.second;
    if (RTX_STRINGS_ARE_EQUAL_CS(name,id)) {
      // already here.
      alreadyInIndex = true;
      break;
    }
  }
  
  if (!alreadyInIndex) {
    
    // insert dummy point, then delete it.
    this->insertSingle(id, Point(time(NULL) - 60*60*24*365));
    stringstream ss;
    ss << "delete from " << id << " where time < now()";
    string url = this->urlForQuery(ss.str(),false);
    JsonDocPtr doc = this->jsonFromPath(url);
    
  }
  */
  
  return true;
}


InfluxDbPointRecord::MetricInfo InfluxDbPointRecord::metricInfoFromName(const std::string &name) {
  
  MetricInfo m;
  size_t firstComma = name.find(",");
  // measure name is everything up to the first comma, even if that's everything
  m.measurement = name.substr(0,firstComma);
  
  if (firstComma != string::npos) {
    // a comma was found. therefore treat the name as tokenized
    string keysValuesStr = name.substr(firstComma+1);
    boost::regex kvReg("([^=]+)=([^,]+),?"); // key - value pair
    boost::sregex_iterator it(keysValuesStr.begin(), keysValuesStr.end(), kvReg), end;
    for ( ; it != end; ++it) {
      m.tags[(*it)[1]] = (*it)[2];
    }
  }
  return m;
}

const string InfluxDbPointRecord::nameFromMetricInfo(RTX::InfluxDbPointRecord::MetricInfo info) {
  stringstream ss;
  ss << info.measurement;
  typedef pair<string,string> stringPair;
  BOOST_FOREACH( stringPair p, info.tags) {
    ss << "," << p.first << "=" << p.second;
  }
  const string name = ss.str();
  return name;
}


const string InfluxDbPointRecord::nameAndWhereClause(const string& name) {
  MetricInfo m = this->metricInfoFromName(name);
  
  stringstream ss;
  ss << "\"" << m.measurement << "\" WHERE ";
  typedef pair<string,string> stringPair;
  
  int i = 0;
  BOOST_FOREACH( stringPair p, m.tags) {
    if (i > 0) {
      ss << " AND ";
    }
    ss << p.first << "='" << p.second << "'";
    ++i;
  }
  
  const string select = ss.str();
  return select;
}



std::vector<Point> InfluxDbPointRecord::selectRange(const std::string& id, time_t startTime, time_t endTime) {
  std::vector<Point> points;
  
  stringstream sqlss;
  
  sqlss << "SELECT * FROM " << this->nameAndWhereClause(id);
  
  sqlss << " and time >= " << startTime << "s";
  sqlss << " and time <= "   << endTime << "s";
  //sqlss << " order asc";
  string url = this->urlForQuery(sqlss.str());

  JsonDocPtr doc = this->jsonFromPath(url);
  return this->pointsFromJson(doc);
}


Point InfluxDbPointRecord::selectNext(const std::string& id, time_t time) {
  
  Point p;
  vector<Point> points;
  time_t margin = 60*60*12;
  time_t max_margin = this->searchDistance();
  time_t lookahead = time + 1;
  
  while (points.size() == 0 && lookahead < time + max_margin) {
    points = this->selectRange(id, lookahead, lookahead + margin);
    lookahead += margin;
  }
  
  // make sure the points are sorted
  std::sort(points.begin(), points.end(), &Point::comparePointTime);
  
  if (points.size() > 0) {
    p = points.front();
    int i = 0;
    while (p.time <= time && i < points.size()) {
      p = points.at(i);
      ++i;
    }
  }
  else {
    cerr << "no points found for " << id << " :: range " << time - 1 << " - " << lookahead + margin << endl;
  }
  
  return p;
  
//  
//  
//  std::vector<Point> points;
//  stringstream sqlss;
//  
//  sqlss << "SELECT * FROM " << this->nameAndWhereClause(id);
//  sqlss << " and time > " << time << "s" << " LIMIT 1";
//  //sqlss << " order asc";
//  string url = this->urlForQuery(sqlss.str());
//  
//  JsonDocPtr doc = this->jsonFromPath(url);
//  points = this->pointsFromJson(doc);
//  
//  if (points.size() == 0) {
//    return Point();
//  }
//  
//  return points.front();
}


Point InfluxDbPointRecord::selectPrevious(const std::string& id, time_t time) {
  
  
  // influx 0.9.1 only supports ORDER ASC - so we have to select iteratively
  
  
  Point p;
  vector<Point> points;
  time_t margin = 60*60*12;
  time_t max_margin = this->searchDistance();
  time_t lookBehind = time - 1;
  string q;
  
  while (points.size() == 0 && lookBehind > time - max_margin) {
    points = this->selectRange(id, lookBehind - margin, lookBehind + 1);
    lookBehind -= margin;
  }
  
  // make sure the points are sorted
  std::sort(points.begin(), points.end(), &Point::comparePointTime);
  
  if (points.size() > 0) {
    p = points.back();
    while ( p.time >= time && points.size() > 0 ) {
      points.pop_back();
      p = points.back();
    }
  }
  else {
    cerr << "no points found for " << id << endl;
  }
  
  return p;

  
//  
//  
//  
//  
//  
//  
//  
//  std::vector<Point> points;
//  stringstream sqlss;
//  
//  sqlss << "SELECT * FROM " << this->nameAndWhereClause(id);
//  sqlss << " and time < " << time << "s" << " LIMIT 1";
//  //sqlss << " order asc";
//  string url = this->urlForQuery(sqlss.str());
//  
//  JsonDocPtr doc = this->jsonFromPath(url);
//  points = this->pointsFromJson(doc);
//  
//  if (points.size() == 0) {
//    return Point();
//  }
//  
//  return points.front();
}



void InfluxDbPointRecord::insertSingle(const std::string& id, Point point) {
  
  vector<Point> points;
  points.push_back(point);
  
  this->insertRange(id, points);
  
}

void InfluxDbPointRecord::insertRange(const std::string& id, std::vector<Point> points) {
  if (points.size() == 0) {
    return;
  }
  
  vector<Point> existing;
  existing = this->selectRange(id, points.front().time - 1, points.back().time + 1);
  map<time_t,bool> existingMap;
  BOOST_FOREACH(const Point& p, existing) {
    existingMap[p.time] = true;
  }
  
  vector<Point> insertionPoints;
  
  BOOST_FOREACH(const Point& p, points) {
    if (existingMap.find(p.time) == existingMap.end()) {
      insertionPoints.push_back(p);
    }
  }
  
  if (insertionPoints.size() == 0) {
    return;
  }
  
  const string content = this->insertionDataFromPoints(id, insertionPoints);
  this->sendPointsWithString(content);
  
  // cache the inserted range.
  BOOST_FOREACH(const Point& p, insertionPoints) {
    if (p.time > _range.second) {
      _range.second = p.time;
    }
    if (p.time < _range.first || _range.first == 0) {
      _range.first = p.time;
    }
  }
  
}


void InfluxDbPointRecord::removeRecord(const std::string& id) {
  // to-do fix this. influx bug related to dropping a series:
//  return;
  
  
  stringstream sqlss;
  sqlss << "DROP SERIES FROM " << this->nameAndWhereClause(id);
  string url = this->urlForQuery(sqlss.str(),false);
  
  JsonDocPtr doc = this->jsonFromPath(url);
  
  
}

void InfluxDbPointRecord::truncate() {
  
  stringstream dropSS;
  dropSS << "DROP DATABASE " << this->db;
  
  stringstream truncateSS;
  truncateSS << "/query?u=" << this->user << "&p=" << this->pass << "&q=" << this->urlEncode("DROP DATABASE " + this->db);
  JsonDocPtr d = this->jsonFromPath(truncateSS.str());
  
  stringstream createSS;
  createSS << "/query?u=" << this->user << "&p=" << this->pass << "&q=" << this->urlEncode("CREATE DATABASE " + this->db);
  JsonDocPtr c = this->jsonFromPath(createSS.str());
  
  
}


vector< PointRecord::nameUnitsPair > InfluxDbPointRecord::identifiersAndUnits() {

  /*
   
   perform a query to get all the series.
   response will be nested in terms of "measurement", and then each array in the "values" array will denote an individual time series:
   
series: [
  {   name: flow
      columns:  [asset_id, asset_type, dma, ... ]
      values: [ [33410,    pump,       brecon, ...],
                [33453,    pipe,       mt.\ washington, ...],
                [...]
              ]
  },
  {   name: pressure
      columns:   [asset_id, asset_type, dma, ...]
      values: [  [44305,    junction,   brecon, ...],
                 [43205,    junction,   mt.\ washington, ...],
                 [...]
              ]
  }
   
   */
  
  
  
  
  vector< nameUnitsPair > seriesList;
  
  if (!this->isConnected()) {
    this->dbConnect();
  }
  if (!this->isConnected()) {
    return seriesList;
  }
  
  
  string q = "show series";
  string url = this->urlForQuery(q,false);
  JsonDocPtr js = this->jsonFromPath(url);
  
  if (js) {
    
    rapidjson::SizeType zero = 0;
    const rapidjson::Value& result = (*js)["results"][zero];
    const rapidjson::Value& series = result["series"];
    rapidjson::Type t = series.GetType();
    if (!series.IsArray()) {
      return seriesList;
    }
    for (rapidjson::SizeType i = 0; i < series.Size(); ++i) {
      // measurement name?
      const rapidjson::Value& thisSeries = series[i];
      const string measureName = thisSeries["name"].GetString();
      const rapidjson::Value& columns = thisSeries["columns"];
      const rapidjson::Value& valuesArr = thisSeries["values"];
      // valuesArr is an array of arrays.
      for (rapidjson::SizeType iVal = 0; iVal < valuesArr.Size(); ++iVal) {
        
        // this is where a time series is defined!
        map<string,string> kv;
        
        const rapidjson::Value& thisTsValues = valuesArr[iVal];
        // size of thisTsValues should == size of columns.
        for (rapidjson::SizeType j = 0; j < thisTsValues.Size(); ++j) {
          const string tsKeyStr = columns[j].GetString();
          const string tsValStr = thisTsValues[j].GetString();
          
          // exclude internal influx _key:
          if (RTX_STRINGS_ARE_EQUAL(tsKeyStr, "_key")) {
            continue;
          }
          
          // exclude empty valued keys
          if (RTX_STRINGS_ARE_EQUAL(tsValStr, "")) {
            continue;
          }
          
          kv[tsKeyStr] = tsValStr;
        }
        
        // now we have all kv pairs that define a time series.
        // do we have units info?
        Units units = RTX_NO_UNITS;
        if (kv.find("units") != kv.end()) {
          units = Units::unitOfType(kv["units"]);
        }
        
        // now assemble the complete name:
        stringstream namestr;
        namestr << measureName;
        typedef pair<string,string> stringPair;
        BOOST_FOREACH(stringPair p, kv) {
          namestr << "," << p.first << "=" << p.second;
        }
        
        // the name has been assembled!
        seriesList.push_back(make_pair(namestr.str(), units));
        
      } // for each values array (ts definition)
    } // for each measurement
  } // if js body exists
  
  return seriesList;
}





vector<Point> InfluxDbPointRecord::pointsFromJson(JsonDocPtr doc) {
  vector<Point> points;
  
  // multiple time series might be returned eventually, but for now it's just a single-value array.
  
  if (doc == NULL) {
    return points;
  }

  if (!doc->HasMember("results")) {
    return points;
  }
  
  const rapidjson::SizeType zero = 0;
  const rapidjson::Value& results = (*doc)["results"];
  if (!results.IsArray() || results.Size() == 0) {
    return points;
  }
  
  const rapidjson::Value& series = results[zero]["series"];
  if (!series.IsArray() || series.Size() == 0) {
    return points;
  }
  
  const rapidjson::Value& tsData = series[zero];
  string measureName = tsData["name"].GetString();
  
  // create a little map so we know what order the columns are in
  map<string,int> columnMap;
  const rapidjson::Value& columns = tsData["columns"];
  for (rapidjson::SizeType i = 0; i < columns.Size(); ++i) {
    string colName = columns[i].GetString();
    columnMap[colName] = (int)i;
  }
  
  int timeIndex = columnMap["time"];
  int valueIndex = columnMap["value"];
  int qualityIndex = columnMap["quality"];
  int confidenceIndex = columnMap["confidence"];
  
  // now go through each returned row and create a point.
  // use the column name map to set point properties.
  const rapidjson::Value& pointRows = tsData["values"];
  if (!pointRows.IsArray() || pointRows.Size() == 0) {
    return points;
  }
  
  points.reserve((size_t)pointRows.Size());
  for (rapidjson::SizeType i = 0; i < pointRows.Size(); ++i) {
    const rapidjson::Value& row = pointRows[i];
    time_t pointTime = (time_t)row[timeIndex].GetInt();
    double pointValue = row[valueIndex].GetDouble();
    Point::PointQuality pointQuality = (row[qualityIndex].IsNull()) ? Point::opc_rtx_override : (Point::PointQuality)row[qualityIndex].GetInt();
    double pointConf = row[confidenceIndex].GetDouble();
    Point p(pointTime, pointValue, pointQuality, pointConf);
    points.push_back(p);
  }
  
  
  
  return points;
}





JsonDocPtr InfluxDbPointRecord::jsonFromPath(const std::string &url) {
  JsonDocPtr documentOut;
  InfluxConnectInfo_t connectionInfo;
  
  connectionInfo.sockStream.connect(this->host, to_string(this->port));
  if (!connectionInfo.sockStream) {
    cerr << "cannot connect" << endl;
    return documentOut;
  }
  
  string body;
  
  {
    connectionInfo.sockStream << "GET " << url << " HTTP/1.0\r\n";
    connectionInfo.sockStream << "Host: " << this->host << "\r\n";
    connectionInfo.sockStream << "Accept: */*\r\n";
    connectionInfo.sockStream << "Connection: close\r\n\r\n";
    connectionInfo.sockStream >> connectionInfo.httpVersion;
    connectionInfo.sockStream >> connectionInfo.statusCode;
    getline(connectionInfo.sockStream, connectionInfo.statusMessage);
    
    string headerStr;
    connectionInfo.sockStream >> headerStr;
    while (std::getline(connectionInfo.sockStream, headerStr) && headerStr != "\r") {
      // nothing
    }
    
    
    std::getline(connectionInfo.sockStream, body);
    cout << connectionInfo.sockStream.rdbuf() << endl;
    connectionInfo.sockStream.flush();
    connectionInfo.sockStream.close();
    
  }
  
  
  documentOut.reset(new rapidjson::Document);
  
  if (connectionInfo.statusCode == 204 /* no content */) {
    return documentOut;
  }
  
  
  documentOut.get()->Parse<0>(body.c_str());
  
  cout << body << endl;
  
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  documentOut->Accept(writer);
  std::cout << buffer.GetString() << std::endl;
  
  return documentOut;
}


const std::string InfluxDbPointRecord::urlEncode(std::string s) {
  
  std::string encStr("");
  
  CURL *curl;
  curl = curl_easy_init();
  if (curl) {
    char *enc;
    enc = curl_easy_escape(curl, s.c_str(), 0);
    encStr = string(enc);
    curl_easy_cleanup(curl);
  }
  
//  cout << s << endl;
  return encStr;
}





const string InfluxDbPointRecord::insertionDataFromPoints(const string& tsName, vector<Point> points) {
  
  /*
   As you can see in the example below, you can post multiple points to multiple series at the same time by separating each point with a new line. Batching points in this manner will result in much higher performance.
   
   curl -i -XPOST 'http://localhost:8086/write?db=mydb' --data-binary '
   cpu_load_short,host=server01,region=us-west value=0.64
   cpu_load_short,host=server02,region=us-west value=0.55 1422568543702900257 
   cpu_load_short,direction=in,host=server01,region=us-west value=23422.0 1422568543702900257'
   
   */
  
  
  stringstream ss;
  
  int i = 0;
  BOOST_FOREACH(const Point& p, points) {
    if (i++ > 0) {
      ss << '\n';
    }
    string valueStr = to_string(p.value);
    if (valueStr.find(".") == string::npos) {
      valueStr.append(".");
    }
    
    ss << tsName << " value=" << valueStr << "," << "quality=" << (int)p.quality << "," << "confidence=" << p.confidence << " " << p.time;
  }
  
  string data = ss.str();
  return data;
}


void InfluxDbPointRecord::sendPointsWithString(const string& content) {
  
  
  // host:port/write?db=my-db&precision=s
  
  stringstream queryss;
  queryss << "/write?db=" << this->db;
  queryss << "&u=" << this->user;
  queryss << "&p=" << this->pass;
  queryss << "&precision=s";
  
  string url("http://localhost:8086");
  
  url.append(queryss.str());
  
  
  InfluxConnectInfo_t connectionInfo;
  connectionInfo.sockStream.connect(this->host, to_string(this->port));
  if (!connectionInfo.sockStream) {
    cerr << "cannot connect" << endl;
    return;
  }
  
  stringstream httpContent;
  
  httpContent << "POST " << url << " HTTP/1.0\r\n";
  httpContent << "Host: " << this->host << "\r\n";
  httpContent << "Accept: */*\r\n";
  httpContent << "Content-Type: text/plain\r\n";
  httpContent << "Content-Length: " << content.length() << "\r\n";
  httpContent << "Connection: close\r\n\r\n";
  httpContent << content;
  httpContent.flush();
  
  // send the data
  connectionInfo.sockStream << httpContent.str();
  
  // get response, process headers
  connectionInfo.sockStream >> connectionInfo.httpVersion;
  connectionInfo.sockStream >> connectionInfo.statusCode;
  getline(connectionInfo.sockStream, connectionInfo.statusMessage);
  
  string body;
  string headerStr;
  while (std::getline(connectionInfo.sockStream, headerStr) && headerStr != "\r") {
    //cout << headerStr << endl;
    // nothing
  }
  
  
  std::getline(connectionInfo.sockStream, body);
  cout << connectionInfo.sockStream.rdbuf() << endl;
  connectionInfo.sockStream.flush();
  connectionInfo.sockStream.close();
  
  
  
  
  
}







//
//
//const string InfluxDbPointRecord::serializedJson(JsonDocPtr doc) {
//  
//  rapidjson::StringBuffer strbuf;
//  rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
//  doc->Accept(writer);
//  
//  string serialized(strbuf.GetString());
//  return serialized;
//}

const string InfluxDbPointRecord::urlForQuery(const std::string& query, bool appendTimePrecision) {
  stringstream queryss;
  queryss << "/query?db=" << this->db;
  queryss << "&u=" << this->user;
  queryss << "&p=" << this->pass;
  queryss << "&q=" << this->urlEncode(query);
  if (appendTimePrecision) {
    queryss << "&epoch=s";
  }
  
  return queryss.str();
}
//
//JsonDocPtr InfluxDbPointRecord::insertionJsonFromPoints(const std::string& tsName, std::vector<Point> points) {
//  
//  JsonDocPtr doc( new rapidjson::Document );
//  rapidjson::Document::AllocatorType& allocator = doc->GetAllocator();
//  
//  doc->SetArray();
//  
//  rapidjson::Value jsName(tsName.c_str());
//  rapidjson::Value jsCols(rapidjson::kArrayType);
//  jsCols.PushBack("time", allocator);
//  jsCols.PushBack("value", allocator);
//  jsCols.PushBack("quality", allocator);
//  jsCols.PushBack("confidence", allocator);
//  
//  rapidjson::Value jsPoints(rapidjson::kArrayType);
//  BOOST_FOREACH(const Point& p, points) {
//    rapidjson::Value jsPoint(rapidjson::kArrayType);
//    jsPoint.PushBack((int)p.time,     allocator);
//    jsPoint.PushBack((double)p.value, allocator);
//    jsPoint.PushBack((int)p.quality,  allocator);
//    jsPoint.PushBack((double)p.confidence, allocator);
//    // add it to the points collection
//    jsPoints.PushBack(jsPoint, allocator);
//  }
//  
//  // put it all together.
//  rapidjson::Value tsData(rapidjson::kObjectType);
//  tsData.AddMember("name", tsName.c_str(), allocator);
//  tsData.AddMember("columns", jsCols, allocator);
//  tsData.AddMember("points", jsPoints, allocator);
//  tsData.AddMember("confidence", jsPoints, allocator);
//  
//  doc->PushBack(tsData, allocator);
//  
//  return doc;
//}
//
//

//
//void InfluxDbPointRecord::postPointsWithBody(const std::string& body) {
//  
//  stringstream queryss;
//  queryss << "/db/" << this->db;
//  queryss << "/series?";
//  queryss << "u=" << this->user;
//  queryss << "&p=" << this->pass;
//  queryss << "&time_precision=s";
//  string url(queryss.str());
//  
//  
//  stringstream portss;
//  portss << this->port;
//  
//  InfluxConnectInfo_t connectionInfo;
//  
//  connectionInfo.sockStream.connect(this->host, portss.str());
//  if (!connectionInfo.sockStream) {
//    cerr << "cannot connect" << endl;
//    return;
//  }
//  
//  connectionInfo.sockStream << "POST " << url << " HTTP/1.0\r\n";
//  connectionInfo.sockStream << "Host: " << this->host << "\r\n";
//  connectionInfo.sockStream << "Accept: */*\r\n";
//  connectionInfo.sockStream << "Content-Type: application/json; charset=utf-8\r\n";
//  connectionInfo.sockStream << "Content-Length: " << body.size() << "\r\n";
//  connectionInfo.sockStream << "Connection: close\r\n\r\n";
//  
//  connectionInfo.sockStream << body;
//  connectionInfo.sockStream.flush();
////  cout << connectionInfo.sockStream.rdbuf();
//  connectionInfo.sockStream.close();
//  
//}
//

