#!/usr/bin/python
# Copyright 2016 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Program to fetch power logging data from a sweetberry device
   or other usb device that exports a USB power logging interface.
"""
from __future__ import print_function
import argparse
import array
import datetime
import json
import os
import struct
import sys
import time
import traceback
from pprint import pprint

import usb

from stats_manager import StatsManager

# This can be overridden by -v.
debug = False
def debuglog(msg):
  if debug:
    print(msg)

def logoutput(msg):
  print(msg)
  sys.stdout.flush()


class Spower(object):
  """Power class to access devices on the bus.

  Usage:
    bus = Spower()

  Instance Variables:
    _dev: pyUSB device object
    _read_ep: pyUSB read endpoint for this interface
    _write_ep: pyUSB write endpoint for this interface
  """

  INA231 = 1

  # Map between header channel number (0-47)
  # and INA I2C bus/addr on sweetberry.
  CHMAP = {
	0: (3, 0x40),
	1: (1, 0x40),
	2: (2, 0x40),
	3: (0, 0x40),
	4: (3, 0x41),
	5: (1, 0x41),
	6: (2, 0x41),
	7: (0, 0x41),
	8: (3, 0x42),
	9: (1, 0x42),
	10: (2, 0x42),
	11: (0, 0x42),
	12: (3, 0x43),
	13: (1, 0x43),
	14: (2, 0x43),
	15: (0, 0x43),
	16: (3, 0x44),
	17: (1, 0x44),
	18: (2, 0x44),
	19: (0, 0x44),
	20: (3, 0x45),
	21: (1, 0x45),
	22: (2, 0x45),
	23: (0, 0x45),
	24: (3, 0x46),
	25: (1, 0x46),
	26: (2, 0x46),
	27: (0, 0x46),
	28: (3, 0x47),
	29: (1, 0x47),
	30: (2, 0x47),
	31: (0, 0x47),
	32: (3, 0x48),
	33: (1, 0x48),
	34: (2, 0x48),
	35: (0, 0x48),
	36: (3, 0x49),
	37: (1, 0x49),
	38: (2, 0x49),
	39: (0, 0x49),
	40: (3, 0x4a),
	41: (1, 0x4a),
	42: (2, 0x4a),
	43: (0, 0x4a),
	44: (3, 0x4b),
	45: (1, 0x4b),
	46: (2, 0x4b),
	47: (0, 0x4b),
  }

  def __init__(self, board, vendor=0x18d1,
               product=0x5020, interface=1, serialname=None):
    self._board = board

    # Find the stm32.
    dev_list = usb.core.find(idVendor=vendor, idProduct=product, find_all=True)
    if dev_list is None:
      raise Exception("Power", "USB device not found")

    # Check if we have multiple stm32s and we've specified the serial.
    dev = None
    if serialname:
      for d in dev_list:
        dev_serial = "PyUSB dioesn't have a stable interface"
        try:
          dev_serial = usb.util.get_string(d, 256, d.iSerialNumber)
        except ValueError:
          # Incompatible pyUsb version.
          dev_serial = usb.util.get_string(d, d.iSerialNumber)
        if dev_serial == serialname:
          dev = d
          break
      if dev is None:
        raise SusbError("USB device(%s) not found" % serialname)
    else:
      try:
        dev = dev_list[0]
      except TypeError:
        # Incompatible pyUsb version.
        dev = dev_list.next()

    debuglog("Found USB device: %04x:%04x" % (vendor, product))
    self._dev = dev

    # Get an endpoint instance.
    try:
      dev.set_configuration()
    except usb.USBError:
      pass
    cfg = dev.get_active_configuration()

    intf = usb.util.find_descriptor(cfg, custom_match=lambda i: \
        i.bInterfaceClass==255 and i.bInterfaceSubClass==0x54)

    self._intf = intf
    debuglog("InterfaceNumber: %s" % intf.bInterfaceNumber)

    read_ep = usb.util.find_descriptor(
      intf,
      # match the first IN endpoint
      custom_match = \
      lambda e: \
          usb.util.endpoint_direction(e.bEndpointAddress) == \
          usb.util.ENDPOINT_IN
    )

    self._read_ep = read_ep
    debuglog("Reader endpoint: 0x%x" % read_ep.bEndpointAddress)

    write_ep = usb.util.find_descriptor(
      intf,
      # match the first OUT endpoint
      custom_match = \
      lambda e: \
          usb.util.endpoint_direction(e.bEndpointAddress) == \
          usb.util.ENDPOINT_OUT
    )

    self._write_ep = write_ep
    debuglog("Writer endpoint: 0x%x" % write_ep.bEndpointAddress)

    self.clear_ina_struct()

    debuglog("Found power logging USB endpoint.")

  def clear_ina_struct(self):
    """ Clear INA description struct."""
    self._inas = []

  def append_ina_struct(self, name, rs, port, addr, data=None):
    """Add an INA descriptor into the list of active INAs.

    Args:
      name:	Readable name of this channel.
      rs:	Sense resistor value in ohms, floating point.
      port:	I2C channel this INA is connected to.
      addr: 	I2C addr of this INA.
      data:	Misc data for special handling, board specific.
    """
    ina = {}
    ina['name'] = name
    ina['rs'] = rs
    ina['port'] = port
    ina['addr'] = addr
    # Calculate INA231 Calibration register
    # (see INA231 spec p.15)
    # CurrentLSB = uA per div = 80mV / (Rsh * 2^15)
    # CurrentLSB uA = 80000000nV / (Rsh mOhm * 0x8000)
    ina['uAscale'] = 80000000. / (rs * 0x8000);
    ina['uWscale'] = 25. * ina['uAscale'];
    ina['data'] = data
    self._inas.append(ina)

  def wr_command(self, write_list, read_count=1, wtimeout=100, rtimeout=1000):
    """Write command to logger logic.

    This function writes byte command values list to stm, then reads
    byte status.

    Args:
      write_list: list of command byte values [0~255].
      read_count: number of status byte values to read.

    Interface:
      write: [command, data ... ]
      read: [status ]

    Returns:
      bytes read, or None on failure.
    """
    debuglog("Spower.wr_command(write_list=[%s] (%d), read_count=%s)" % (
          list(bytearray(write_list)), len(write_list), read_count))

    # Clean up args from python style to correct types.
    write_length = 0
    if write_list:
      write_length = len(write_list)
    if not read_count:
      read_count = 0

    # Send command to stm32.
    if write_list:
      cmd = write_list
      ret = self._write_ep.write(cmd, wtimeout)

    debuglog("RET: %s " % ret)

    # Read back response if necessary.
    if read_count:
      bytesread = self._read_ep.read(512, rtimeout)
      debuglog("BYTES: [%s]" % bytesread)

      if len(bytesread) != read_count:
        pass

      debuglog("STATUS: 0x%02x" % int(bytesread[0]))
      if read_count == 1:
        return bytesread[0]
      else:
        return bytesread

    return None

  def clear(self):
    """Clear pending reads on the stm32"""
    try:
      while True:
        ret = self.wr_command("", read_count=512, rtimeout=100, wtimeout=50)
        debuglog("Try Clear: read %s" % "success" if ret == 0 else "failure")
    except:
      pass

  def send_reset(self):
    """Reset the power interface on the stm32"""
    cmd = struct.pack("<H", 0x0000)
    ret = self.wr_command(cmd, rtimeout=50, wtimeout=50)
    debuglog("Command RESET: %s" % "success" if ret == 0 else "failure")

  def reset(self):
    """Try resetting the USB interface until success

    Throws:
      Exception on failure.
    """
    count = 10
    while count > 0:
      self.clear()
      try:
        self.send_reset()
        count = 0
      except Exception as e:
        self.clear()
        self.clear()
        debuglog("TRY %d of 10: %s" % (count, e))
      finally:
        count -= 1
    if count == 0:
      raise Exception("Power", "Failed to reset")

  def stop(self):
    """Stop any active data acquisition."""
    cmd = struct.pack("<H", 0x0001)
    ret = self.wr_command(cmd)
    debuglog("Command STOP: %s" % "success" if ret == 0 else "failure")

  def start(self, integration_us):
    """Start data acquisition.

    Args:
      integration_us: int, how many us between samples, and
                      how often the data block must be read.

    Returns:
      actual sampling interval in ms.
    """
    cmd = struct.pack("<HI", 0x0003, integration_us)
    read = self.wr_command(cmd, read_count=5)
    actual_us = 0
    if len(read) == 5:
      ret, actual_us = struct.unpack("<BI", read)
      debuglog("Command START: %s %dus" % ("success" if ret == 0 else "failure", actual_us))
    else:
      debuglog("Command START: FAIL")

    return actual_us

  def add_ina_name(self, name):
    """Add INA from board config.

    Args:
      name:	readable name of power rail in board config.

    Returns:
      True if INA added, False if the INA is not on this board.

    Raises:
      Exception on unexpected failure.
    """
    for datum in self._brdcfg:
      if datum["name"] == name:
        channel = int(datum["channel"])
        rs = int(float(datum["rs"]) * 1000.)
        board = datum["sweetberry"]

        if board == self._board:
          port, addr = self.CHMAP[channel]
          self.add_ina(port, self.INA231, addr, 0, rs, data=datum)
          return True
        else:
          return False
    raise Exception("Power", "Failed to find INA %s" % name)

  def set_time(self, timestamp_us):
    """Set sweetberry time to match host time.

    Args:
      timestamp_us: host timestmap in us.
    """
    # 0x0005 , 8 byte timestamp
    cmd = struct.pack("<HQ", 0x0005, timestamp_us)
    ret = self.wr_command(cmd)

    debuglog("Command SETTIME: %s" % "success" if ret == 0 else "failure")

  def add_ina(self, bus, ina_type, addr, extra, resistance, data=None):
    """Add an INA to the data acquisition list.

    Args:
      bus: which i2c bus the INA is on. Same ordering as Si2c.
      ina_type: which model INA. 0x1 -> INA231
      addr: 7 bit i2c addr of this INA
      extra: extra data for nonstandard configs.
      resistance: int, shunt resistance in mOhm
    """
    # 0x0002, 1B: bus, 1B:INA type, 1B: INA addr, 1B: extra, 4B: Rs
    cmd = struct.pack("<HBBBBI", 0x0002, bus, ina_type, addr, extra, resistance)
    ret = self.wr_command(cmd)
    if ret == 0:
      if data:
        name = data['name']
      else:
        name = "ina%d_%02x" % (bus, addr)
      self.append_ina_struct(name, resistance, bus, addr, data=data)
    debuglog("Command ADD_INA: %s" % "success" if ret == 0 else "failure")

  def report_header_size(self):
    """Helper function to calculate power record header size."""
    result = 2
    timestamp = 8
    return result + timestamp

  def report_size(self, ina_count):
    """Helper function to calculate full power record size."""
    record = 2

    datasize = self.report_header_size() + ina_count * record
    # Round to multiple of 4 bytes.
    datasize = int(((datasize + 3) / 4) * 4)

    return datasize

  def read_line(self):
    """Read a line of data from the setup INAs

    Return:
      list of dicts of the values read, otherwise None.
      [{ts:100, vbat:450}, {ts:200, vbat:440}]
    """
    try:
      expected_bytes = self.report_size(len(self._inas))
      cmd = struct.pack("<H", 0x0004)
      bytesread = self.wr_command(cmd, read_count=expected_bytes)
    except usb.core.USBError as e:
      print("READ LINE FAILED %s" % e)
      return None

    if len(bytesread) == 1:
      if bytesread[0] != 0x6:
        debuglog("READ LINE FAILED bytes: %d ret: %02x" % (
            len(bytesread), bytesread[0]))
      return None

    if len(bytesread) % expected_bytes != 0:
      debuglog("READ LINE WARNING: expected %d, got %d" % (
          expected_bytes, len(bytesread)))

    packet_count = len(bytesread) / expected_bytes

    values = []
    for i in range(0, packet_count):
      start = i * expected_bytes
      end = (i + 1) * expected_bytes
      record = self.interpret_line(bytesread[start:end])
      values.append(record)

    return values

  def interpret_line(self, data):
    """Interpret a power record from INAs

    Args:
      data: one single record of bytes.

    Output:
      stdout of the record in csv format.

    Return:
      dict containing name, value of recorded data.
    """
    status, size = struct.unpack("<BB", data[0:2])
    if len(data) != self.report_size(size):
      print("READ LINE FAILED st:%d size:%d expected:%d len:%d" % (
          status, size, self.report_size(size), len(data)))
    else:
      pass

    timestamp = struct.unpack("<Q", data[2:10])[0]
    debuglog("READ LINE: st:%d size:%d time:%dus" % (status, size, timestamp))
    ftimestamp = float(timestamp) / 1000000.

    record = {"ts": ftimestamp, "status": status, "berry":self._board}

    for i in range(0, size):
      idx = self.report_header_size() + 2*i
      raw_w = struct.unpack("<H", data[idx:idx+2])[0]
      uw = raw_w * self._inas[i]['uWscale']
      name = self._inas[i]['name']
      debuglog("READ %d %s: %fs: %fuW" % (i, name, ftimestamp, uw))
      record[self._inas[i]['name']] = uw

    return record

  def load_board(self, brdfile):
    """Load a board config.

    Args:
      brdfile:	Filename of a json file decribing the INA wiring of this board.
    """
    with open(brdfile) as data_file:
        data = json.load(data_file)

    #TODO: validate this.
    self._brdcfg = data;
    if debug:
      pprint(data)


class powerlog(object):
  """Power class to log aggregated power.

  Usage:
    obj = powerlog()

  Instance Variables:
    _data: records sweetberries readings and calculates statistics.
    _pwr[]: Spower objects for individual sweetberries.
  """

  def __init__(self, brdfile, cfgfile, serial_a=None, serial_b=None,
               sync_date=False, use_ms=False, use_mW=False, print_stats=False,
               save_stats=False, save_raw_data=False):
    """
    Args:
      brdfile: string name of json file containing board layout.
      cfgfile: string name of json containing list of rails to read.
      serial_a: serial number of sweetberry A.
      serial_b: serial number of sweetberry B.
      sync_date: report timestamps synced with host datetime.
      use_ms: report timestamps in ms rather than us.
    """
    self._data = StatsManager()
    self._pwr = {}
    self._use_ms = use_ms
    self._use_mW = use_mW
    self._print_stats = print_stats
    self._save_stats = save_stats
    self._save_raw_data = save_raw_data

    if not serial_a and not serial_b:
      self._pwr['A'] = Spower('A')
    if serial_a:
      self._pwr['A'] = Spower('A', serialname=serial_a)
    if serial_b:
      self._pwr['B'] = Spower('B', serialname=serial_b)

    with open(cfgfile) as data_file:
      names = json.load(data_file)
    self._names = names

    for key in self._pwr:
      self._pwr[key].load_board(brdfile)
      self._pwr[key].reset()

    # Allocate the rails to the appropriate boards.
    used_boards = []
    for name in self._names:
      success = False
      for key in self._pwr.keys():
        if self._pwr[key].add_ina_name(name):
          success = True
          if key not in used_boards:
            used_boards.append(key)
      if not success:
        raise Exception("Failed to add %s (maybe missing "
                        "sweetberry, or bad board file?)" % name)

    # Evict unused boards.
    for key in self._pwr.keys():
      if key not in used_boards:
        self._pwr.pop(key)

    for key in self._pwr.keys():
      if sync_date:
        self._pwr[key].set_time(time.time() * 1000000)
      else:
        self._pwr[key].set_time(0)

  def start(self, integration_us_request, seconds, sync_speed=.8):
    """
    Starts sampling.

    Args:
      integration_us_request: requested interval between sample values.
      seconds: time until exit, or None to run until cancel.
      sync_speed: A usb request is sent every [.8] * integration_us.
    """
    # We will get back the actual integration us.
    # It should be the same for all devices.
    integration_us = None
    for key in self._pwr:
      integration_us_new = self._pwr[key].start(integration_us_request)
      if integration_us:
        if integration_us != integration_us_new:
          raise Exception("FAIL",
              "Integration on A: %dus != integration on B %dus" % (
              integration_us, integration_us_new))
      integration_us = integration_us_new

    # CSV header
    title = "ts:%dus" % integration_us
    for name in self._names:
      unit = "mW" if self._use_mW else "uW"
      title += ", %s %s" % (name, unit)
    title += ", status"
    logoutput(title)

    forever = False
    if not seconds:
      forever = True
    end_time = time.time() + seconds
    try:
      pending_records = []
      while forever or end_time > time.time():
        if (integration_us > 5000):
          time.sleep((integration_us / 1000000.) * sync_speed)
        for key in self._pwr:
          records = self._pwr[key].read_line()
          if not records:
            continue

          for record in records:
            pending_records.append(record)

        pending_records.sort(key=lambda r: r['ts'])

        aggregate_record = {"boards": set()}
        for record in pending_records:
          if record["berry"] not in aggregate_record["boards"]:
            for rkey in record.keys():
              aggregate_record[rkey] = record[rkey]
            aggregate_record["boards"].add(record["berry"])
          else:
            print("break %s, %s" % (record["berry"],
                                    aggregate_record["boards"]))
            break

          if aggregate_record["boards"] == set(self._pwr.keys()):
            csv = "%f" % aggregate_record["ts"]
            for name in self._names:
              if name in aggregate_record:
                multiplier = 0.001 if self._use_mW else 1
                power = aggregate_record[name] * multiplier
                csv += ", %.2f" % power
                self._data.AddValue(name, power)
              else:
                csv += ", "
            csv += ", %d" % aggregate_record["status"]
            logoutput(csv)

            aggregate_record = {"boards": set()}
            for r in range(0, len(self._pwr)):
              pending_records.pop(0)

    except KeyboardInterrupt:
      print('\nCTRL+C caught.')

    finally:
      for key in self._pwr:
        self._pwr[key].stop()
      self._data.CalculateStats()
      if self._print_stats:
        self._data.PrintSummary()
      save_dir = datetime.datetime.now().strftime('Sweetberry%Y%m%d%H%M%S')
      save_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                              save_dir)
      if self._save_stats:
        self._data.SaveSummary(save_dir)
      if self._save_raw_data:
        self._data.SaveRawData(save_dir)


def main():
  # Command line argument description.
  parser = argparse.ArgumentParser(
      description="Gather CSV data from sweetberry")
  parser.add_argument('-b', '--board', type=str,
      help="Board configuration file, eg. my.board", default="")
  parser.add_argument('-c', '--config', type=str,
      help="Rail config to monitor, eg my.scenario", default="")
  parser.add_argument('-A', '--serial', type=str,
      help="Serial number of sweetberry A", default="")
  parser.add_argument('-B', '--serial_b', type=str,
      help="Serial number of sweetberry B", default="")
  parser.add_argument('-t', '--integration_us', type=int,
      help="Target integration time for samples", default=100000)
  parser.add_argument('-s', '--seconds', type=float,
      help="Seconds to run capture. Overrides -n", default=0.)
  parser.add_argument('--date', default=False,
      help="Sync logged timestamp to host date", action="store_true")
  parser.add_argument('--ms', default=False,
      help="Print timestamp as milliseconds", action="store_true")
  parser.add_argument('--mW', default=False,
      help="Print power as milliwatts, otherwise default to microwatts",
      action="store_true")
  parser.add_argument('--slow', default=False,
      help="Intentionally overflow", action="store_true")
  parser.add_argument('--print_stats', default=False,
      help="Print statistics for sweetberry readings at the end",
      action="store_true")
  parser.add_argument('--save_stats', default=False,
      help="Save statistics for sweetberry readings",
      action="store_true")
  parser.add_argument('--save_raw_data', default=False,
      help="Save raw data for sweetberry readings",
      action="store_true")
  parser.add_argument('-v', '--verbose', default=False,
      help="Very chatty printout", action="store_true")

  args = parser.parse_args()

  global debug
  if args.verbose:
    debug = True

  integration_us_request = args.integration_us
  if not args.board:
    raise Exception("Power", "No board file selected, see board.README")
  if not args.config:
    raise Exception("Power", "No config file selected, see board.README")

  brdfile = args.board
  cfgfile = args.config
  seconds = args.seconds
  serial_a = args.serial
  serial_b = args.serial_b
  sync_date = args.date
  use_ms = args.ms
  use_mW = args.mW
  print_stats = args.print_stats
  save_stats = args.save_stats
  save_raw_data = args.save_raw_data

  boards = []

  sync_speed = .8
  if args.slow:
    sync_speed = 1.2

  # Set up logging interface.
  powerlogger = powerlog(brdfile, cfgfile, serial_a=serial_a,
      serial_b=serial_b, sync_date=sync_date, use_ms=use_ms, use_mW=use_mW,
      print_stats=print_stats, save_stats=save_stats,
      save_raw_data=save_raw_data)

  # Start logging.
  powerlogger.start(integration_us_request, seconds, sync_speed=sync_speed)


if __name__ == "__main__":
  main()


