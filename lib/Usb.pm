package Usb;
# USB device state.
#   use Usb;  Usb::mounted(), Usb::suspended(), Usb::remote_wakeup(), Usb::cdc_connected()
use strict;
sub mounted       { return Usb::_mounted() }
sub suspended     { return defined &Usb::_suspended ? Usb::_suspended() : 0 }
sub remote_wakeup { return defined &Usb::_remote_wakeup ? Usb::_remote_wakeup() : 0 }
sub cdc_connected { return defined &Usb::_cdc_connected ? Usb::_cdc_connected() : 1 }
1;
