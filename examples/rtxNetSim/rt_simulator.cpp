#include <iostream>

#include "SqliteProjectFile.h"
#include "SqlitePointRecord.h"


using namespace std;
using namespace RTX;

int main(int argc, const char * argv[])
{
  
  
  ProjectFile::sharedPointer project( new SqliteProjectFile());
  
  project->loadProjectFile("/Users/sam/Desktop/milly.rtx");
  
  Model::sharedPointer model = project->model();
  
  model->initDMAs();
  model->initEngine();
  
  
  cout << "RTX Project File Info:" << endl;
  cout << *(model) << endl;
  
  
  model->overrideControls();
  model->setTanksNeedReset(true);
  model->runExtendedPeriod(00000, 00000);
  
  
  
  
  
  
  
}

