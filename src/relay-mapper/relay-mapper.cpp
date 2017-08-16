#include "relay-mapper.h"
#include "relay-mapper_p.h"

// ************************** QRelayMapperPrivate

QRelayMapperPrivate::QRelayMapperPrivate()
{
    pLogicalInfoArray = Q_NULLPTR;
    ui16MaxPhysicalPinHandled = 0;
    CallbackQueryLowLayerBusy = nullptr;
    CallbackStartLowLayerSwitch = nullptr;
}

QRelayMapperPrivate::~QRelayMapperPrivate()
{
}

// ************************** QRelayMapper

QRelayMapper::QRelayMapper(QObject *parent) :
    QRelayBase(parent, new QRelayMapperPrivate())
{
    Q_D(QRelayMapper);

    connect(&d->sliceTimer, &QTimer::timeout, this, &QRelayMapper::onSliceTimer);
}

void QRelayMapper::setup(quint16 ui16LogicalArrayInfoCount,
                         const struct TLogicalRelayEntry *pLogicalInfoArray,
                         int iMsecSlice,
                         RelayStartLowLayerSwitchFunction CallbackStartLowLayerSwitch)
{
    Q_D(QRelayMapper);

    QRelayBase::setupBaseBitmaps(ui16LogicalArrayInfoCount);

    d->CallbackStartLowLayerSwitch = CallbackStartLowLayerSwitch;
    // Setup logical bitmap sizes / workers
    d->logicalSetMaskCurrent = QBitArray(ui16LogicalArrayInfoCount);
    d->arrPinDelayCounter.resize(ui16LogicalArrayInfoCount);

    // estimate max physical bit number
    for(quint16 ui16Entry=0; ui16Entry<ui16LogicalArrayInfoCount; ui16Entry++)
    {
        d->ui16MaxPhysicalPinHandled = qMax(d->ui16MaxPhysicalPinHandled, pLogicalInfoArray[ui16Entry].ui16OnPosition);
        d->ui16MaxPhysicalPinHandled = qMax(d->ui16MaxPhysicalPinHandled, pLogicalInfoArray[ui16Entry].ui16OffPosition);
    }
    // keep pointer to setup data
    d->pLogicalInfoArray = pLogicalInfoArray;

    // keep slice period
    d->slicePeriod = iMsecSlice;
}

void QRelayMapper::setupCallbackLowLayerBusy(RelayQueryLowLayerBusy callback)
{
    Q_D(QRelayMapper);
    d->CallbackQueryLowLayerBusy = callback;
}

const QBitArray &QRelayMapper::getLogicalRelayState()
{
    Q_D(QRelayMapper);
    return d->logicalSetMaskCurrent;
}

void QRelayMapper::startSet(quint16 ui16BitNo,
                            bool bSet,
                            bool bForce)
{
    QRelayBase::startSet(ui16BitNo, bSet, bForce);

    Q_D(QRelayMapper);
    // relay mapper performs all activity on slice timer
    if(!d->sliceTimer.isActive())
    {
        d->sliceTimer.setInterval(0);
        d->sliceTimer.setSingleShot(true);
        d->sliceTimer.start();
    }
}

void QRelayMapper::idleCleanup()
{
    QRelayBase::idleCleanup();

    Q_D(QRelayMapper);
    d->sliceTimer.stop();
}

void QRelayMapper::onSliceTimer()
{
    Q_D(QRelayMapper);
    // in case low layer is still busy - wait for next slice
    if(d->CallbackQueryLowLayerBusy && d->CallbackQueryLowLayerBusy())
        return;
    // First timer -> turn on periodic
    if(d->sliceTimer.interval() == 0)
    {
        d->sliceTimer.setInterval(d->slicePeriod);
        d->sliceTimer.setSingleShot(false);
        d->sliceTimer.start();
    }
    // prepare output data
    QBitArray physicalEnableMask(d->ui16MaxPhysicalPinHandled+1);
    QBitArray physicalSetMask(d->ui16MaxPhysicalPinHandled+1);
    // loop all logical bits
    bool bLowerIORequested = false;
    for(int iBit=0; iBit<getLogicalRelayCount(); iBit++)
    {
        const TLogicalRelayEntry& logicalPinInfoEntry = d->pLogicalInfoArray[iBit];
        // 1. Handle changes since last slice
        if(d->logicalEnableMaskNext.at(iBit))
        {
            // set physical pins
            // by using a default the 'else'-part in the decisions below are not
            // required
            bool bSetOutput = true;
            // Check On/off
            quint16 ui16OutBitPosition;
            if(d->logicalSetMaskNext.at(iBit))
            {
                ui16OutBitPosition = logicalPinInfoEntry.ui16OnPosition;
                // ON state same for bistable/monostable
                if(logicalPinInfoEntry.ui8Flags & (1<<RELAY_PHYS_NEG_ON))
                    bSetOutput = false;
            }
            else
            {
                // OFF state Different behaviour for mono-/bistable
                if(logicalPinInfoEntry.ui8Flags & (1<<RELAY_BISTABLE))
                {
                    ui16OutBitPosition = logicalPinInfoEntry.ui16OffPosition;
                    if(logicalPinInfoEntry.ui8Flags & (1<<RELAY_PHYS_NEG_OFF))
                        bSetOutput = false;
                }
                else
                {
                    // uinstable sets only ON pin
                    ui16OutBitPosition = logicalPinInfoEntry.ui16OnPosition;
                    if((logicalPinInfoEntry.ui8Flags & (1<<RELAY_PHYS_NEG_ON)) == 0)
                        bSetOutput = false;
                }
            }
            // Do set output
            physicalEnableMask.setBit(ui16OutBitPosition, true);
            physicalSetMask.setBit(ui16OutBitPosition, bSetOutput);

            // Setup timer (+1: timer is deleted below - so zero values finish here)
            d->arrPinDelayCounter[iBit] = logicalPinInfoEntry.ui8OnTime+1;
            // mark busy
            d->logicalBusyMask.setBit(iBit);
            // keep state
            d->logicalSetMaskCurrent.setBit(iBit, d->logicalSetMaskNext.at(iBit));
            // set handled
            d->logicalEnableMaskNext.clearBit(iBit);
            // we need a low layer transaction
            bLowerIORequested = true;
        }
        // 2. Handle busy logical bits
        if(d->logicalBusyMask.at(iBit))
        {
            // pin delay finished
            quint8 ui8Counter = d->arrPinDelayCounter[iBit];
            ui8Counter--;
            d->arrPinDelayCounter[iBit] = ui8Counter;
            if(ui8Counter == 0)
            {
                // Only bistable relay have to be touched
                if(logicalPinInfoEntry.ui8Flags & (1<<RELAY_BISTABLE))
                {
                    uint16_t ui16OutBitPosition;
                    bool bSetOutput = true;
                    if(d->logicalSetMaskCurrent.at(iBit))
                    {
                        // It is ON
                        ui16OutBitPosition = logicalPinInfoEntry.ui16OnPosition;
                        if((logicalPinInfoEntry.ui8Flags & (1<<RELAY_PHYS_NEG_ON)) == 0)
                            bSetOutput = false;
                    }
                    else
                    {
                        // It is OFF
                        ui16OutBitPosition = logicalPinInfoEntry.ui16OffPosition;
                        if((logicalPinInfoEntry.ui8Flags & (1<<RELAY_PHYS_NEG_OFF)) == 0)
                            bSetOutput = false;
                    }

                    // Do set output
                    physicalEnableMask.setBit(ui16OutBitPosition, true);
                    physicalSetMask.setBit(ui16OutBitPosition, bSetOutput);
                    // we need a low layer transaction
                    bLowerIORequested = true;
                }
                // Mark this bit as finished
                d->logicalBusyMask.clearBit(iBit);
            }
        }
    }
    bool bLowerLayerBusy = false;
    if(bLowerIORequested)
    {
        if(d->CallbackStartLowLayerSwitch)
            bLowerLayerBusy = d->CallbackStartLowLayerSwitch(physicalEnableMask, physicalSetMask, this);
    }
    // There is either nothing to do for low layer or low layer signalled blocked call (so it is finished)
    if(!bLowerLayerBusy)
        onLowLayerIdle();
}
