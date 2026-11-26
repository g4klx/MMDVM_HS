/*
 *   Copyright (C) 2015,2016,2020 by Jonathan Naylor G4KLX
 *   Copyright (C) 2016,2017,2018,2019,2020 by Andy Uribe CA6JAU
 *   Copyright (C) 2017 by Danilo DB4PLE
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "Config.h"
#include "Globals.h"
#include "IO.h"

uint32_t    m_frequency_rx;
uint32_t    m_frequency_tx;
uint32_t    m_pocsag_freq_tx;
uint8_t     m_power;

CIO::CIO():
m_started(false),
m_rxBuffer(1024U),
m_txBuffer(1024U),
m_LoDevYSF(false),
m_ledCount(0U),
m_scanEnable(false),
m_scanPauseCnt(0U),
m_scanPos(0U),
m_ledValue(true),
m_lightsOff(false),
m_srvStyle(SRV_BLINK),
m_srvInverted(false),
m_srvBuiltInverted(false),
m_quietScan(false),
m_callLights(true),
m_rxtxLights(true),
m_shown(STATE_IDLE),
m_shownScan(false),
m_rxOn(false),
m_txOn(false),
m_watchdog(0U),
m_int1counter(0U),
m_int2counter(0U)
{
  // The lights as this board's build sets them, until the host says.
  m_srvStyle    = lightsBuilt(1U);
  m_quietScan   = lightsBuilt(2U) == 1U;
  m_pair[0U]    = lightsBuilt(5U);
  m_pair[1U]    = lightsBuilt(6U);
  m_pair[2U]    = lightsBuilt(7U);
#if defined(SRV_LED_INVERTED) || defined(CONSTANT_SRV_LED_INVERTED) || defined(DISCREET_SRV_LED_INVERTED)
  m_srvBuiltInverted = true;
#endif
  m_srvInverted = m_srvBuiltInverted;
  for (uint8_t i = 0U; i <= STATE_M17; i++)
    m_rxOffset[i] = m_txOffset[i] = 0;

  Init();

  CE_pin(HIGH);
  LED_pin(HIGH);
  PTT_pin(LOW);
  DSTAR_pin(LOW);
  DMR_pin(LOW);
  YSF_pin(LOW);
  P25_pin(LOW);
  NXDN_pin(LOW);
  M17_pin(LOW);
  POCSAG_pin(LOW);
  COS_pin(LOW);
  DEB_pin(LOW);

#if !defined(BIDIR_DATA_PIN)
  TXD_pin(LOW);
#endif

  SCLK_pin(LOW);
  SDATA_pin(LOW);
  SLE_pin(LOW);

  selfTest();

  m_modeTimerCnt = 0U;
}

void CIO::selfTest()
{
  bool ledValue = false;
  uint32_t ledCount = 0U;
  uint32_t blinks = 0U;

  while(true) {
    ledCount++;
    delay_us(1000U);

    if(ledCount >= 125U) {
      ledCount = 0U;
      ledValue = !ledValue;

      LED_pin(!ledValue);
      PTT_pin(ledValue);
      DSTAR_pin(ledValue);
      DMR_pin(ledValue);
      YSF_pin(ledValue);
      P25_pin(ledValue);
      NXDN_pin(ledValue);
      M17_pin(ledValue);
      POCSAG_pin(ledValue);
      COS_pin(ledValue);

      blinks++;

      if(blinks > 5U)
        break;
    }
  }
}

void CIO::process()
{
  uint8_t bit;
  uint32_t scantime;
  uint8_t  control;

  m_ledCount++;

  if (m_started) {
    // Two seconds timeout
    if (m_watchdog >= 19200U) {
      if (m_modemState == STATE_DSTAR || m_modemState == STATE_DMR || m_modemState == STATE_YSF || m_modemState == STATE_P25 || m_modemState == STATE_NXDN || m_modemState == STATE_M17) {
        m_modemState = STATE_IDLE;
        setMode(m_modemState);
      }

      m_watchdog = 0U;
    }

    serviceLight();
  } else {
    if (m_ledCount >= 240000U) {
      m_ledCount = 0U;
      m_ledValue = !m_ledValue;
      LED_pin(m_ledValue);
    }
    return;
  }

  // Switch off the transmitter if needed
  if (m_txBuffer.getData() == 0U && m_tx) {
    if(m_cwid_state) { // check for CW ID end of transmission
      m_cwid_state = false;
      // Restoring previous mode
      if (m_TotalModes)
        io.ifConf(m_modemState_prev, true);
    }
    if(m_pocsag_state) { // check for POCSAG end of transmission
      m_pocsag_state = false;
      // Restoring previous mode
      if (m_TotalModes)
        io.ifConf(m_modemState_prev, true);
    }
    setRX(false);
  }

  if(m_modemState_prev == STATE_DSTAR)
    scantime = SCAN_TIME;
  else if(m_modemState_prev == STATE_DMR)
    scantime = SCAN_TIME * 2U;
  else if(m_modemState_prev == STATE_YSF)
    scantime = SCAN_TIME;
  else if(m_modemState_prev == STATE_P25)
    scantime = SCAN_TIME;
  else if(m_modemState_prev == STATE_NXDN)
    scantime = SCAN_TIME;
  else if(m_modemState_prev == STATE_M17)
    scantime = SCAN_TIME;
  else
    scantime = SCAN_TIME;

  if(m_modeTimerCnt >= scantime) {
    m_modeTimerCnt = 0U;
    if( (m_modemState == STATE_IDLE) && (m_scanPauseCnt == 0U) && m_scanEnable && !m_cwid_state && !m_pocsag_state) {
      m_scanPos = (m_scanPos + 1U) % m_TotalModes;
      showScan(m_Modes[m_scanPos]);
      io.ifConf(m_Modes[m_scanPos], true);
    }
  }

  if (m_rxBuffer.getData() >= 1U) {
    m_rxBuffer.get(bit, control);

    switch (m_modemState_prev) {
      case STATE_DSTAR:
        dstarRX.databit(bit);
        break;
      case STATE_DMR:
#if defined(DUPLEX)
        if (m_duplex) {
          if (m_tx)
            dmrRX.databit(bit, control);
          else
            dmrIdleRX.databit(bit);
        } else
          dmrDMORX.databit(bit);
#else
        dmrDMORX.databit(bit);
#endif
        break;
      case STATE_YSF:
        ysfRX.databit(bit);
        break;
      case STATE_P25:
        p25RX.databit(bit);
        break;
      case STATE_NXDN:
        nxdnRX.databit(bit);
        break;
      case STATE_M17:
        m17RX.databit(bit);
        break;
      default:
        break;
    }

  }
}

void CIO::start()
{
  m_TotalModes = 0U;

  if(m_dstarEnable) {
    m_Modes[m_TotalModes] = STATE_DSTAR;
    m_TotalModes++;
  }
  if(m_dmrEnable) {
    m_Modes[m_TotalModes] = STATE_DMR;
    m_TotalModes++;
  }
  if(m_ysfEnable) {
    m_Modes[m_TotalModes] = STATE_YSF;
    m_TotalModes++;
  }
  if(m_p25Enable) {
    m_Modes[m_TotalModes] = STATE_P25;
    m_TotalModes++;
  }
  if(m_nxdnEnable) {
    m_Modes[m_TotalModes] = STATE_NXDN;
    m_TotalModes++;
  }
  if(m_m17Enable) {
    m_Modes[m_TotalModes] = STATE_M17;
    m_TotalModes++;
  }

#if defined(ENABLE_SCAN_MODE)
  if(m_TotalModes > 1U)
    m_scanEnable = true;
  else {
    m_scanEnable = false;
    setMode(m_modemState);
  }
#else
  m_scanEnable = false;
  setMode(m_modemState);
#endif

  if (m_started)
    return;

  startInt();

  m_started = true;
}

void CIO::write(uint8_t* data, uint16_t length, const uint8_t* control)
{
  if (!m_started)
    return;

  for (uint16_t i = 0U; i < length; i++) {
    if (control == NULL)
      m_txBuffer.put(data[i], MARK_NONE);
    else
      m_txBuffer.put(data[i], control[i]);
  }

  // Switch the transmitter on if needed
  if (!m_tx) {
    setTX();
    m_tx = true;
  }
}

uint16_t CIO::getSpace() const
{
  return m_txBuffer.getSpace();
}

bool CIO::hasTXOverflow()
{
  return m_txBuffer.hasOverflowed();
}

bool CIO::hasRXOverflow()
{
  return m_rxBuffer.hasOverflowed();
}

#if defined(ZUMSPOT_ADF7021) || defined(LONESTAR_USB) || defined(SKYBRIDGE_HS)
void CIO::checkBand(uint32_t frequency_rx, uint32_t frequency_tx) {
  if (!(io.hasSingleADF7021())) {
    // There are two ADF7021s on the board
    if (io.isDualBand()) {
      // Dual band
      if ((frequency_tx <= VHF2_MAX) && (frequency_rx <= VHF2_MAX)) {
        // Turn on VHF side
        io.setBandVHF(true);
      } else if ((frequency_tx >= UHF1_MIN) && (frequency_rx >= UHF1_MIN)) {
        // Turn on UHF side
        io.setBandVHF(false);
      }
    }
  }
}

uint8_t CIO::checkZUMspot(uint32_t frequency_rx, uint32_t frequency_tx) {
  if (!(io.hasSingleADF7021())) {
    // There are two ADF7021s on the board
    if (io.isDualBand()) {
      // Dual band
      if ((frequency_tx <= VHF2_MAX) && (frequency_rx <= VHF2_MAX)) {
        // Turn on VHF side
        io.setBandVHF(true);
      } else if ((frequency_tx >= UHF1_MIN) && (frequency_rx >= UHF1_MIN)) {
        // Turn on UHF side
        io.setBandVHF(false);
      }
    } else if (!io.isDualBand()) {
      // Duplex board
      if ((frequency_tx < UHF1_MIN) || (frequency_rx < UHF1_MIN)) {
        // Reject VHF frequencies
        return 4U;
      }
    }
  }
  return 0U;
}
#endif

uint8_t CIO::setFreq(uint32_t frequency_rx, uint32_t frequency_tx, uint8_t rf_power, uint32_t pocsag_freq_tx)
{
  // Configure power level
  setPower(rf_power);

#if !defined(DISABLE_FREQ_CHECK)
  // Check frequency ranges
  if( !( ((frequency_rx >= VHF1_MIN)&&(frequency_rx < VHF1_MAX)) || ((frequency_tx >= VHF1_MIN)&&(frequency_tx < VHF1_MAX)) || \
  ((frequency_rx >= UHF1_MIN)&&(frequency_rx < UHF1_MAX)) || ((frequency_tx >= UHF1_MIN)&&(frequency_tx < UHF1_MAX)) || \
  ((frequency_rx >= VHF2_MIN)&&(frequency_rx < VHF2_MAX)) || ((frequency_tx >= VHF2_MIN)&&(frequency_tx < VHF2_MAX)) || \
  ((frequency_rx >= UHF2_MIN)&&(frequency_rx < UHF2_MAX)) || ((frequency_tx >= UHF2_MIN)&&(frequency_tx < UHF2_MAX)) ) )
    return 4U;

  if( !( ((pocsag_freq_tx >= VHF1_MIN)&&(pocsag_freq_tx < VHF1_MAX)) || \
  ((pocsag_freq_tx >= UHF1_MIN)&&(pocsag_freq_tx < UHF1_MAX)) || \
  ((pocsag_freq_tx >= VHF2_MIN)&&(pocsag_freq_tx < VHF2_MAX)) || \
  ((pocsag_freq_tx >= UHF2_MIN)&&(pocsag_freq_tx < UHF2_MAX)) ) )
    return 4U;
#endif

#if !defined(DISABLE_FREQ_BAN)
  // Check banned frequency ranges
  if( ((frequency_rx >= BAN1_MIN)&&(frequency_rx <= BAN1_MAX)) || ((frequency_tx >= BAN1_MIN)&&(frequency_tx <= BAN1_MAX)) || \
  ((frequency_rx >= BAN2_MIN)&&(frequency_rx <= BAN2_MAX)) || ((frequency_tx >= BAN2_MIN)&&(frequency_tx <= BAN2_MAX)) )
    return 4U;

  if( ((pocsag_freq_tx >= BAN1_MIN)&&(pocsag_freq_tx <= BAN1_MAX)) || \
  ((pocsag_freq_tx >= BAN2_MIN)&&(pocsag_freq_tx <= BAN2_MAX)) )
    return 4U;
#endif

// Check if we have a single, dualband or duplex board
#if defined(ZUMSPOT_ADF7021) || defined(LONESTAR_USB) || defined(SKYBRIDGE_HS)
  if (checkZUMspot(frequency_rx, frequency_tx) > 0) {
    return 4U;
  }
#endif

  // Configure frequency
  m_frequency_rx = frequency_rx;
  m_frequency_tx = frequency_tx;
  m_pocsag_freq_tx = pocsag_freq_tx;

  return 0U;
}

void CIO::setOffsets(MMDVM_STATE modemState, int16_t rx, int16_t tx)
{
  if (modemState > STATE_IDLE && modemState <= STATE_M17 && modemState != STATE_POCSAG) {
    m_rxOffset[modemState] = rx;
    m_txOffset[modemState] = tx;
  }
}

void CIO::setMode(MMDVM_STATE modemState)
{
  m_shown     = modemState;
  m_shownScan = false;
  modeLights();
}

// The mode the scan listens for now: its light, unless the mode lights
// stay dark while idle.
void CIO::showScan(MMDVM_STATE modemState)
{
  m_shown     = modemState;
  m_shownScan = true;
  modeLights();
}

// WPSD: the lights' fields as this board's build sets them (Config.h), for
// LIGHTS_BUILT: 1 the service light, 2 the mode lights while idle (1 =
// dark), 5-7 the lights for NXDN, M17 and POCSAG.
uint8_t CIO::lightsBuilt(uint8_t field) const
{
  switch (field) {
    case 1U:
#if defined(CONSTANT_SRV_LED) || defined(CONSTANT_SRV_LED_INVERTED)
      return SRV_STEADY;
#elif defined(DISCREET_SRV_LED) || defined(DISCREET_SRV_LED_INVERTED)
      return SRV_BRIEF;
#else
      return SRV_BLINK;
#endif
    case 2U:
#if defined(QUIET_MODE_LEDS)
      return 1U;
#else
      return 0U;
#endif
    case 5U:
#if defined(USE_ALTERNATE_NXDN_LEDS)
      return PAIR_YSF_P25;
#else
      return PAIR_OWN;
#endif
    case 6U:
#if defined(USE_ALTERNATE_M17_LEDS)
      return PAIR_DSTAR_P25;
#else
      return PAIR_OWN;
#endif
    case 7U:
#if defined(USE_ALTERNATE_POCSAG_LEDS)
      return PAIR_DSTAR_DMR;
#else
      return PAIR_OWN;
#endif
    default:
      return 0U;
  }
}

// MMDVM_SET_LIGHTS: eight bytes, or nine.
//   0  1 = every light dark, whatever the rest says
//   1  the service light: SRV_*
//   2  the mode lights while idle: 0 = each as the scan listens for it, 1 = dark
//   3  the mode lights during a call: 1 = on, 0 = dark
//   4  the RX and TX lights: 1 = on, 0 = dark
//   5  the lights for NXDN: PAIR_*
//   6  the lights for M17: PAIR_*
//   7  the lights for POCSAG: PAIR_*
//   8  1 = the service light the other way round from the build (optional)
// 1, 2 and 5-7 also take LIGHTS_BUILT. Applied at once.
uint8_t CIO::setLights(const uint8_t* data, uint8_t length)
{
  if (length < 8U)
    return 4U;
  if (data[0U] > 1U || data[3U] > 1U || data[4U] > 1U)
    return 4U;
  if (data[1U] > SRV_OFF && data[1U] != LIGHTS_BUILT)
    return 4U;
  if (data[2U] > 1U && data[2U] != LIGHTS_BUILT)
    return 4U;
  for (uint8_t i = 5U; i < 8U; i++) {
    if (data[i] > PAIR_DSTAR_DMR && data[i] != LIGHTS_BUILT)
      return 4U;
  }
  // Some boards light the service light on the pin's other level.
  if (length > 8U && data[8U] > 1U)
    return 4U;

  m_lightsOff  = data[0U] == 1U;
  m_srvStyle   = data[1U] == LIGHTS_BUILT ? lightsBuilt(1U) : data[1U];
  m_quietScan  = (data[2U] == LIGHTS_BUILT ? lightsBuilt(2U) : data[2U]) == 1U;
  m_callLights = data[3U] == 1U;
  m_rxtxLights = data[4U] == 1U;
  for (uint8_t i = 0U; i < 3U; i++)
    m_pair[i] = data[5U + i] == LIGHTS_BUILT ? lightsBuilt(5U + i) : data[5U + i];
  m_srvInverted = m_srvBuiltInverted != (length > 8U && data[8U] == 1U);

  // The service light shows the change at once: a brief blink starts its
  // cycle lit (serviceLight puts it out after its moment).
  m_ledCount = 0U;
  if (m_srvStyle == SRV_BRIEF && !m_lightsOff)
    LED_pin(!m_srvInverted);
  serviceLight();
  modeLights();
  COS_pin(m_rxOn && m_rxtxLights && !m_lightsOff);
  PTT_pin(m_txOn && m_rxtxLights && !m_lightsOff);

  return 0U;
}

// The service light, called each tick: m_ledCount counts the ticks.
void CIO::serviceLight()
{
  // The pin level that lights it.
  const bool on = !m_srvInverted;

  if (m_lightsOff || m_srvStyle == SRV_OFF) {
    LED_pin(!on);
  } else if (m_srvStyle == SRV_STEADY) {
    LED_pin(on);
  } else if (m_srvStyle == SRV_BRIEF) {
    if (m_ledCount == 10000U)
      LED_pin(!on);
    if (m_ledCount >= 480000U) {
      m_ledCount = 0U;
      LED_pin(on);
    }
  } else if (m_ledCount >= 24000U) {
    m_ledCount = 0U;
    m_ledValue = !m_ledValue;
    LED_pin(m_ledValue);
  }
}

// The mode lights for m_shown. NXDN and M17 share one light on every
// board; each pin is written once, so neither puts out the other's.
void CIO::modeLights()
{
  bool dstar = false, dmr = false, ysf = false, p25 = false, nxdn = false, pocsag = false;

  bool lit = !m_lightsOff && (m_shownScan ? !m_quietScan : m_callLights);
  if (lit) {
    uint8_t pair = 0xFFU;
    switch (m_shown) {
      case STATE_DSTAR:  dstar = true; break;
      case STATE_DMR:    dmr = true; break;
      case STATE_YSF:    ysf = true; break;
      case STATE_P25:    p25 = true; break;
      case STATE_NXDN:   pair = m_pair[0U]; if (pair == PAIR_OWN) nxdn = true; break;
      case STATE_M17:    pair = m_pair[1U]; if (pair == PAIR_OWN) nxdn = true; break;
      case STATE_POCSAG: pair = m_pair[2U]; if (pair == PAIR_OWN) pocsag = true; break;
      default: break;
    }
    switch (pair) {
      case PAIR_YSF_P25:   ysf = p25 = true; break;
      case PAIR_DSTAR_P25: dstar = p25 = true; break;
      case PAIR_DSTAR_DMR: dstar = dmr = true; break;
      default: break;
    }
  }

  DSTAR_pin(dstar);
  DMR_pin(dmr);
  YSF_pin(ysf);
  P25_pin(p25);
  NXDN_pin(nxdn);
  POCSAG_pin(pocsag);
}

// The TX light (the PTT pin), as the transmitter goes on and off.
void CIO::txLight(bool on)
{
  m_txOn = on;
  PTT_pin(on && m_rxtxLights && !m_lightsOff);
}

void CIO::setDecode(bool dcd)
{
  if (dcd != m_dcd) {
    m_scanPauseCnt = 1U;
    m_rxOn = dcd;
    COS_pin(dcd && m_rxtxLights && !m_lightsOff);
  }

  m_dcd = dcd;
}

void CIO::setLoDevYSF(bool on)
{
  m_LoDevYSF = on;
}

void CIO::resetWatchdog()
{
  m_watchdog = 0U;
}

uint32_t CIO::getWatchdog()
{
  return m_watchdog;
}

void CIO::getIntCounter(uint16_t &int1, uint16_t &int2)
{
  int1 = m_int1counter;
  int2 = m_int2counter;
  m_int1counter = 0U;
  m_int2counter = 0U;
}
