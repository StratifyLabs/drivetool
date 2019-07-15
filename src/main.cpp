#include <stdio.h>

#include <sapi/sys.hpp>
#include <sapi/hal.hpp>
#include <sapi/var.hpp>

#include "sl_config.h"

static Printer printer;

static void show_usage(const Cli & cli);

bool execute_blank_check(const Drive & drive, bool is_erase = false);

int main(int argc, char * argv[]){
	String action;
	String address;
	String is_help;
	String size;
	String path;

	Cli cli(argc, argv);
	cli.set_publisher(SL_CONFIG_PUBLISHER);
	cli.handle_version();

	printer.set_verbose_level(Printer::INFO);

	action = cli.get_option("action", "specify the operation blankcheck|erase|eraseall|getinfo|read");
	is_help = cli.get_option("help", "show help options");
	address = cli.get_option("address", "specify the address to read or erase");
	size = cli.get_option("size", "specify the number of bytes to read or erase");
	path = cli.get_option("path", "specify the path to the drive such as /dev/drive0");
	printer.set_verbose_level( cli.get_option("verbose") );

	if( is_help.is_empty() == false ){
		cli.show_options();
		exit(0);
	}

	Drive drive;

	if( path.is_empty() ){
		printer.error("'path' must be specified");
		cli.show_options();
		exit(1);
	}

	drive.set_keep_open(false);

	if( drive.open(path) < 0 ){
		printer.error("Failed to open drive at '%s'", path.cstring());
		exit(1);
	}

	printer.open_object(action);

	if( action == "blankcheck" ){
		//erase the device
		execute_blank_check(drive, false);
	} else if ( action == "erase" ){
		if( drive.erase_blocks(address.to_integer(), address.to_integer()) < 0 ){
			printer.error("failed to erase block at address 0x%lX", address);
		} else {
			while( drive.is_busy() ){
				;
			}
			printer.info("block erased at address 0x%lX\n", address);
		}

	} else if ( action == "eraseall" ){
		execute_blank_check(drive, true);
	} else if ( action == "getinfo" ){
		DriveInfo info = drive.get_info();

		printer.key("path", path);
		printer << info;

	} else if ( action == "read" ){

	} else {
		printer.close_object();
		show_usage(cli);
		exit(1);
	}


	printer.close_object();

	return 0;
}


void show_usage(const Cli & cli){
	printer.open_object(String().format("%s usage:", cli.name().cstring()));
	printf("\n");
	cli.show_options();
	exit(1);
}

bool execute_blank_check(const Drive &drive, bool is_erase){
	DriveInfo info = drive.get_info();
	const u32 page_size = 1024;
	Data buffer(page_size);
	Data check(page_size);

	check.fill(0xff);

	printer.info("Blank checking %ld blocks", (u32)info.write_block_count());
	for(u64 i=0; i < info.size(); i+= page_size ){
		u32 loc = drive.loc();

		if( i % 1024*1024 == 0 ){
			printer.debug("checking block at address 0x%lX", i);
		}

		if( drive.read(buffer) != page_size ){
			printer.error("failed to read drive at %ld", drive.loc());
			return false;
		}

		if( buffer != check ){

			if( is_erase ){
				if( drive.erase_blocks(loc, loc) < 0 ){
					printer.error("failed to erase block at address %ld", loc);
					return false;
				}

				while( drive.is_busy() ){
					chrono::wait_milliseconds(5);
				}

				if( drive.read(loc, buffer) != page_size ){
					printer.error("failed to read drive at %ld after erase", drive.loc());
					return false;
				}

			} else {

				printer.error("drive is not blank at %ld", drive.loc());
				return false;
			}
		}

	}

	return true;
}



