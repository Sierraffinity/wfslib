/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <cstdio>
#include <vector>
#include <fstream>
#include <memory>
#include <iostream>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>

#include <wfslib/WfsLib.h>

int main(int argc, char *argv[]) {
	try {
		boost::program_options::options_description desc("Allowed options");
		std::string wfs_path;
		desc.add_options()
			("help", "produce help message")
			("srcimage", boost::program_options::value<std::string>(), "wfs image file (source)")
			("dstimage", boost::program_options::value<std::string>(), "wfs image file (destination)")
			("srcotp", boost::program_options::value<std::string>(), "otp file (source)")
			("dstotp", boost::program_options::value<std::string>(), "otp file (destination)")
			("srcseeprom", boost::program_options::value<std::string>(), "seeprom file (source, required if usb)")
			("dstseeprom", boost::program_options::value<std::string>(), "seeprom file (destination, required if usb)")
			("mlc", "device is mlc (default: device is usb)")
			("usb", "device is usb")
			;

		boost::program_options::variables_map vm;
		boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
		boost::program_options::notify(vm);

		bool bad = false;
		if (!vm.count("srcimage")) { std::cerr << "Missing source wfs image file (--srcimage)" << std::endl; bad = true; }
		if (!vm.count("dstimage")) { std::cerr << "Missing destination wfs image file (--dstimage)" << std::endl; bad = true; }
		if (!vm.count("srcotp")) { std::cerr << "Missing source otp file (--srcotp)" << std::endl; bad = true; }
		if (!vm.count("dstotp")) { std::cerr << "Missing destination otp file (--dstotp)" << std::endl; bad = true; }
		if ((!vm.count("srcseeprom") && !vm.count("mlc"))) { std::cerr << "Missing source seeprom file (--seeprom)" << std::endl; bad = true; }
		if ((!vm.count("dstseeprom") && !vm.count("mlc"))) { std::cerr << "Missing destination seeprom file (--seeprom)" << std::endl; bad = true; }
		if (vm.count("mlc") + vm.count("usb") > 1) { std::cerr << "Can't specify both --mlc and --usb" << std::endl; bad = true; }
		if (vm.count("help") || bad) {
			std::cout << "Usage: wfs-recryptor --srcimage <source wfs image> --dstimage <destination wfs image> --srcotp <source otp path> --dstotp <destination otp path> [--srcseeprom <source seeprom path>] [--dstseeprom <destination seeprom path>] [--mlc] [--usb]" << std::endl;
			std::cout << desc << "\n";
			return 1;
		}

		std::vector<uint8_t> srckey, dstkey;
		std::unique_ptr<OTP> srcotp, dstotp;
		// open otps
		try {
			srcotp.reset(OTP::LoadFromFile(vm["srcotp"].as<std::string>()));
			dstotp.reset(OTP::LoadFromFile(vm["dstotp"].as<std::string>()));
		}
		catch (std::exception& e) {
			std::cerr << "Failed to open OTP: " << e.what() << std::endl;
			return 1;
		}

		if (vm.count("mlc")) {
			// mlc
			srckey = srcotp->GetMLCKey();
			dstkey = dstotp->GetMLCKey();
		}
		else {
			// usb
			std::unique_ptr<SEEPROM> srcseeprom, dstseeprom;
			try {
				srcseeprom.reset(SEEPROM::LoadFromFile(vm["srcseeprom"].as<std::string>()));
				dstseeprom.reset(SEEPROM::LoadFromFile(vm["dstseeprom"].as<std::string>()));
			}
			catch (std::exception& e) {
				std::cerr << "Failed to open SEEPROM: " << e.what() << std::endl;
				return 1;
			}
			srckey = srcseeprom->GetUSBKey(*srcotp);
			dstkey = dstseeprom->GetUSBKey(*dstotp);
		}
		
		auto srcdevice = std::make_shared<FileDevice>(vm["srcimage"].as<std::string>(), 9, false);
		auto dstdevice = std::make_shared<FileDevice>(vm["dstimage"].as<std::string>(), 9, false);
		Wfs::DetectDeviceSectorSizeAndCount(srcdevice, srckey);
		Wfs::DetectDeviceSectorSizeAndCount(dstdevice, dstkey);

		auto file = Wfs(srcdevice, srckey).GetFile(vm["inject-path"].as<std::string>());
		auto file = Wfs(dstdevice, dstkey).GetFile(vm["inject-path"].as<std::string>());
		if (!file) {
			std::cerr << "Error: Didn't find file " << vm["inject-path"].as<std::string>() << " in wfs" << std::endl;
			return 1;
		}
		if (file_size > file->GetSizeOnDisk()) {
			std::cerr << "Error: File to inject too big (wanted size: " << file_size << " bytes, available size: " << file->GetSizeOnDisk() << ")" << std::endl;
			return 1;
		}
		File::stream stream(file);
		std::vector<char> data(0x2000);
		size_t to_copy = file_size;
		while (to_copy > 0) {
			input_file.read((char*)&*data.begin(), std::min(data.size(), to_copy));
			auto read = input_file.gcount();
			if (read <= 0) {
				std::cerr << "Error: Failed to read file to inject" << std::endl;
				return 1;
			}
			stream.write(&*data.begin(), read);
			to_copy -= static_cast<size_t>(read);
		}
		input_file.close();
		stream.close();
		if (file_size < file->GetSize()) {
			file->Resize(file_size);
		}
		std::cout << "Done!" << std::endl;
	}
	catch (std::exception& e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return 1;
	}
	return 0;
}