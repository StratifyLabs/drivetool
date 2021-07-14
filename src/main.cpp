#include <stdio.h>

#include <chrono.hpp>
#include <fs.hpp>
#include <hal.hpp>
#include <printer.hpp>
#include <sys.hpp>
#include <var.hpp>

#include "sl_config.h"

static Printer print;

static void show_usage(const Cli &cli);

class Options {
public:
  Options(const Cli &cli) {
    if (const auto arg = cli.get_option(
            "path", "specify the path to the drive such as /dev/drive0");
        arg.is_empty() == false) {
      m_path = arg;
    }

    if (const auto arg = cli.get_option(
            "action",
            "specify the operation "
            "blankcheck|erase|eraseall|erasedevice|getinfo|read|reset");
        arg.is_empty() == false) {
      m_action = arg;
    }

    if (const auto arg = cli.get_option("period"); arg.is_empty() == false) {
      m_period = arg.to_integer() * 1_milliseconds;
    } else {
      m_period = 500_milliseconds;
    }

    if (const auto arg =
            cli.get_option("address", "specify the address to read or erase");
        arg.is_empty() == false) {
      m_address = arg.to_unsigned_long(StringView::Base::auto_);
    }

    if (const auto arg = cli.get_option(
            "size", "specify the number of bytes to read or erase");
        arg.is_empty() == false) {
      m_size = arg.to_unsigned_long(StringView::Base::auto_);
    }
  }

private:
  API_AC(Options, IdString, action);
  API_AC(Options, PathString, path);
  API_AC(Options, MicroTime, period);
  API_AF(Options, u32, address, 0);
  API_AF(Options, u32, size, 0);
};

bool execute_blank_check(const Drive &drive, bool is_erase = false);
bool execute_read(const Drive &drive, u32 address, u32 size);

int main(int argc, char *argv[]) {
  Cli cli(argc, argv);

  const auto show_help = Cli::ShowHelp()
                             .set_publisher("Stratify Labs, Inc")
                             .set_version(SL_CONFIG_VERSION_STRING);

  print.set_verbose_level(Printer::Level::info);

  Options options(cli);
  const auto is_help = cli.get_option("help", "show help options");
  print.set_verbose_level(cli.get_option("verbose"));
  if (is_help.is_empty() == false) {
    cli.show_help(show_help);
    exit(0);
  }


  if (options.path().is_empty()) {
    print.error("`path` must be specified");
    cli.show_help(show_help);
    exit(1);
  }

  if (FileSystem().exists(options.path()) == false) {
    print.error("invalid `path`: " | options.path());
    cli.show_help(show_help);
    exit(1);
  }

  Drive drive(options.path());

  drive.set_attributes(Drive::Attributes().set_flags(Drive::Flags::initialize));

  if( drive.is_error() ){
    print.error("Failed to initialize drive");
    exit(1);
  }

  const auto info = drive.get_info();

  {
    Printer::Object po(print, options.action());

    if (options.action() == "blankcheck") {
      // erase the device
      execute_blank_check(drive, false);
    } else if (options.action() == "erase") {

      if (drive.unprotect().return_value() < 0) {
        print.error("failed to disable device protection");
      } else {

        if (drive.erase_blocks(options.address(), options.address()).return_value() < 0) {
          print.error(GeneralString().format("failed to erase block at address 0x%lX",
                       options.address()));
        } else {
          while (drive.is_busy()) {
            chrono::wait(1_milliseconds);
          }
          print.info(GeneralString().format("block erased at address 0x%lX\n", options.address()));
        }
      }

    } else if (options.action() == "eraseall") {
      if (drive.unprotect().return_value() < 0) {
        print.error("failed to disable device protection");
      } else {
        execute_blank_check(drive, true);
      }
    } else if (options.action() == "erasedevice") {
      print.info(GeneralString().format("estimated erase time is " F32U "ms",
                  info.erase_device_time().milliseconds()));
      if (drive.unprotect().return_value() < 0) {
        print.error("failed to disable device protection");
      } else {

        drive.erase_device();

        if (drive.is_error()) {
          print.error("failed to erase the device");
        }

        print.info("waiting for erase to complete");
        while (drive.is_busy()) {
          chrono::wait(1_milliseconds);
        }

        print.info("device erase complete");
      }

    } else if (options.action() == "getinfo") {

      print.key("path", options.path());
      print << info;

    } else if (options.action() == "read") {

      execute_read(drive, options.address(), options.size());

    } else if (options.action() == "reset") {

      if (drive.reset().return_value() < 0) {
        print.error("failed to reset drive");
      } else {
        info.erase_block_time().wait();
        drive.is_busy();
        print.info("drive successfully reset");
      }
    }
  }

  return 0;
}

bool execute_read(const Drive &drive, u32 address, u32 size) {
  const u32 page_size = 1024;

  char buffer_array[page_size];

  u32 bytes_read = 0;
  drive.seek(address);
  print.set_flags(Printer::Flags::blob | Printer::Flags::hex);
  do {
    View buffer(buffer_array);
    if (size - bytes_read < page_size) {
      buffer = View(buffer_array, size - bytes_read);
    }
    drive.read(buffer);
    if (drive.return_value() > 0) {
      bytes_read += size;
      print << buffer;
    }
  } while (drive.return_value() == page_size && bytes_read < size);

  if (drive.return_value() < 0) {
    print.error("failed to read drive");
    return false;
  }

  return true;
}

bool execute_blank_check(const Drive &drive, bool is_erase) {
  const auto info = drive.get_info();
  const u32 page_size = 1024;
  u8 buffer_array[page_size];
  u8 check_array[page_size];

  View buffer(buffer_array);
  View check(check_array);

  check.fill<u8>(0xff);

  drive.seek(0);

  print.info(GeneralString().format("Blank checking %u blocks",
                                    info.num_write_blocks()));
  for (u64 i = 0; i < info.size(); i += page_size) {

    u32 loc = drive.file().location();

    if ((i % (1024 * 1024)) == 0) {
      print.debug(GeneralString().format("checking block at address 0x%lX", i));
    }

    buffer.fill(0xaa);
    if (drive.read(buffer).return_value() != page_size) {
      print.error(GeneralString().format("failed to read drive at %ld",
                                         drive.file().location()));
      return false;
    }

    if (buffer != check) {

      drive.seek(loc);

      for (u32 i = 0; i < buffer.count<u32>(); i++) {
        if (buffer.at_const_u32(i) != 0xffffffff) {
          print.debug(GeneralString().format("buffer at [%d] is 0x%08lX", i,
                                             buffer.at_const_u32(i)));
        }
      }

      if (is_erase) {
        print.debug(GeneralString().format("erasing block at address 0x%lX", loc));
        if (drive.erase_blocks(loc, loc).return_value() < 0) {
          print.error(GeneralString().format(
              "failed to erase block at address %ld", loc));
          return false;
        }

        while (drive.is_busy()) {
          chrono::wait(5_milliseconds);
        }

        if (drive.seek(loc).read(buffer).return_value() != page_size) {
          print.error(
              GeneralString().format("failed to read drive at %ld after erase",
                                     drive.file().location()));
          return false;
        }

      } else {

        print.error(GeneralString().format("drive is not blank at %ld", drive.file().location()));
        return false;
      }
    }
  }

  print.info("drive is blank");
  return true;
}
