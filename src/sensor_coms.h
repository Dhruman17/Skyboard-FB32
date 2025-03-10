#ifndef SENSOR_COMS
#define SENSOR_COMS
#include "Wire.h"
void tcaselect(uint8_t i)
{
    if (i > 7)
        return;
    Wire.beginTransmission(TCAADDR);
    Wire.write(1 << i);
    Wire.endTransmission();
}
namespace device
{
    float aref = 3.3; // Vref, this is for 3.3v compatible controller boards, for Arduino use 5.0v.
}

namespace sensor
{
    float ec = 0;
    unsigned int tds = 0;
    float ecCalibration = 1;
}
#endif //