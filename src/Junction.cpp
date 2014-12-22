//
//  Junction.cpp
//  epanet-rtx
//
//  Created by the EPANET-RTX Development Team
//  See README.md and license.txt for more information
//  

#include <iostream>

#include "Junction.h"
#include "OffsetTimeSeries.h"
#include "GainTimeSeries.h"

using namespace RTX;

Junction::Junction(const std::string& name) : Node(name) {
  //_demand = 0;
  //_baseDemand = 0;
  setType(JUNCTION);
  
  // we have nothing yet
  _headState.reset(new TimeSeries());
  _pressureState.reset(new TimeSeries());
  _qualityState.reset(new TimeSeries());
  _demandState.reset(new TimeSeries());
  
  _headState->setUnits(RTX_METER);
  _qualityState->setUnits(RTX_SECOND);
  _demandState->setUnits(RTX_LITER_PER_SECOND);
  
  _headState->setName("N " + name + " head");
  _qualityState->setName("N " + name + " quality");
  _demandState->setName("N " + name + " demand");
  
  _baseDemand = 0;
  _initialQuality = 0;
}
Junction::~Junction() {
  
}

double Junction::baseDemand() {
  return _baseDemand;
}

void Junction::setBaseDemand(double demand) {
  _baseDemand = demand;
}

double Junction::initialQuality() {
  return _initialQuality;
}

void Junction::setInitialQuality(double quality) {
  _initialQuality = quality;
}

void Junction::setRecord(PointRecord::sharedPointer record) {
  _headState->setRecord(record);
  _pressureState->setRecord(record);
  _qualityState->setRecord(record);
  _demandState->setRecord(record);
}

// states - these recieve simulation results from the Model containing class
TimeSeries::sharedPointer Junction::head() {
  return _headState;
}
TimeSeries::sharedPointer Junction::pressure() {
  return _pressureState;
}
TimeSeries::sharedPointer Junction::quality() {
  return _qualityState;
}
TimeSeries::sharedPointer Junction::demand() {
  return _demandState;
}

// measured demand (boundary flow condition)
bool Junction::doesHaveBoundaryFlow() {
  return (this->boundaryFlow() ? true : false);
}
void Junction::setBoundaryFlow(TimeSeries::sharedPointer flow) {
  _boundaryFlow = flow;
}
TimeSeries::sharedPointer Junction::boundaryFlow() {
  return _boundaryFlow;
}

// head measurement
bool Junction::doesHaveHeadMeasure() {
  return (this->headMeasure() ? true : false);
}
void Junction::setHeadMeasure(TimeSeries::sharedPointer headMeas) {
  if (!headMeas || !headMeas->units().isSameDimensionAs(RTX_METER)) {
    return;
  }
  
  _headMeasure = headMeas;
  
  OffsetTimeSeries::sharedPointer relativeHead( new OffsetTimeSeries() );
  relativeHead->setUnits(this->head()->units());
  relativeHead->setOffset(this->elevation() * -1.);
  relativeHead->setSource(headMeas);
  
  // pressure depends on elevation --> head = mx(pressure) + elev
  GainTimeSeries::sharedPointer gainTs( new GainTimeSeries() );
  gainTs->setUnits(RTX_PASCAL);
  gainTs->setGainUnits( RTX_PASCAL / RTX_METER);
  gainTs->setGain(9804.13943198467193);
  gainTs->setSource(relativeHead);
  
  _pressureMeasure = gainTs;
}
TimeSeries::sharedPointer Junction::headMeasure() {
  return _headMeasure;
}

void Junction::setPressureMeasure(TimeSeries::sharedPointer pressure) {
  if (!pressure || !pressure->units().isSameDimensionAs(RTX_PASCAL)) {
    return;
  }
  _pressureMeasure = pressure;
  // pressure depends on elevation --> head = mx(pressure) + elev
  GainTimeSeries::sharedPointer gainTs( new GainTimeSeries() );
  gainTs->setUnits(this->head()->units());
  gainTs->setGainUnits( RTX_METER / RTX_PASCAL );
  gainTs->setGain(0.0001019977334);
  gainTs->setSource(pressure);
  
  OffsetTimeSeries::sharedPointer headMeas( new OffsetTimeSeries() );
  headMeas->setUnits(this->head()->units());
  headMeas->setOffset(this->elevation());
  headMeas->setSource(gainTs);
  
  _headMeasure = headMeas;
}

TimeSeries::sharedPointer Junction::pressureMeasure() {
  return _pressureMeasure;
}


// quality measurement
bool Junction::doesHaveQualityMeasure() {
  return (this->qualityMeasure() ? true : false);
}
void Junction::setQualityMeasure(TimeSeries::sharedPointer quality) {
  _qualityMeasure = quality;
}
TimeSeries::sharedPointer Junction::qualityMeasure() {
  return _qualityMeasure;
}

// quality boundary condition
bool Junction::doesHaveQualitySource() {
  return (this->qualitySource() ? true : false);
}
void Junction::setQualitySource(TimeSeries::sharedPointer quality) {
  _qualityBoundary = quality;
}
TimeSeries::sharedPointer Junction::qualitySource() {
  return _qualityBoundary;
}
