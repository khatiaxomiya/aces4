#include "gtest/gtest.h"
#include "siox_reader.h"
#include "io_utils.h"
#include "setup_reader.h"

#include "sip_tables.h"
#include "interpreter.h"
#include "setup_interface.h"
#include "sip_interface.h"
#include "data_manager.h"

#include "sip_server.h"
#include "sip_mpi_attr.h"
#include "global_state.h"

#include "block.h"

#ifdef HAVE_TAU
#include <TAU.h>
#endif

void list_block_map();
static const std::string dir_name("src/sialx/test/");


TEST(Sial_Simple,Simple_Put_Test){
	std::cout << "****************************************\n";
	sip::DataManager::scope_count=0;
	//create setup_file
	std::string job("put_test");
	std::cout << "JOBNAME = " << job << std::endl;
	int norb = 1;

	{	init_setup(job.c_str());
		set_constant("norb",norb);
		std::string tmp = job + ".siox";
		const char* nm = tmp.c_str();
		add_sial_program(nm);
		int segs[]  = {3};
		set_aoindex_info(1,segs);
		finalize_setup();
	}

	setup::BinaryInputFile setup_file(job + ".dat");
	setup::SetupReader setup_reader(setup_file);
	std::cout << "SETUP READER DATA:\n" << setup_reader<< std::endl;

	//get siox name from setup, load and print the sip tables
	std::string prog_name = setup_reader.sial_prog_list_.at(0);
	std::string siox_dir(dir_name);
	setup::BinaryInputFile siox_file(siox_dir + prog_name);
	sip::SipTables sipTables(setup_reader, siox_file);
	std::cout << "SIP TABLES" << '\n' << sipTables << std::endl;

	sip::SIPMPIAttr &sip_mpi_attr = sip::SIPMPIAttr::get_instance();

	//interpret the program
	sip::PersistentArrayManager<sip::Block,sip::Interpreter> wpam;
	sip::PersistentArrayManager<sip::ServerBlock,sip::SIPServer> spam;

	sip::DataDistribution data_distribution(sipTables, sip_mpi_attr);
	sip::GlobalState::set_program_name(prog_name);
	sip::GlobalState::increment_program();
	if (sip_mpi_attr.is_server()){
		sip::SIPServer server(sipTables, data_distribution, sip_mpi_attr, &spam);
		server.run();

	} else {
		sip::SialxTimer sialxTimer(sipTables.max_timer_slots());
		sip::Interpreter runner(sipTables, sialxTimer, &wpam);
		runner.interpret();
		ASSERT_EQ(0, sip::DataManager::scope_count);

		std::cout << "\nSIAL PROGRAM TERMINATED"<< std::endl;

		int x_slot = sip::Interpreter::global_interpreter->array_slot(std::string("x"));
		int y_slot = sip::Interpreter::global_interpreter->array_slot(std::string("y"));
		sip::index_selector_t x_indices, y_indices;
		for (int i = 0; i < MAX_RANK; i++)
			x_indices[i] = y_indices[i] = sip::unused_index_value;
		x_indices[0] = y_indices[0] = 1;
		x_indices[1] = y_indices[0] = 1;
		sip::BlockId x_bid(x_slot, x_indices);
		sip::BlockId y_bid(y_slot, y_indices);

		sip::Block::BlockPtr x_bptr = sip::Interpreter::global_interpreter->get_block_for_reading(x_bid);
		sip::Block::dataPtr x_data = x_bptr->get_data();
		sip::Block::BlockPtr y_bptr = sip::Interpreter::global_interpreter->get_block_for_reading(y_bid);
		sip::Block::dataPtr y_data = y_bptr->get_data();


		for (int i=0; i<3*3; i++){
			ASSERT_EQ(1, x_data[i]);
			ASSERT_EQ(2, y_data[i]);
		}

	}
}

TEST(Sial,DISABLED_persistent_empty_mpi){

	sip::SIPMPIAttr &sip_mpi_attr = sip::SIPMPIAttr::get_instance();
	int my_rank = sip_mpi_attr.global_rank();

		sip::DataManager::scope_count=0;
		//create setup_file
		std::string job("persistent_empty_mpi");
		std::cout << "JOBNAME = " << job << std::endl;
		double x = 3.456;
		int norb = 4;

		{	init_setup(job.c_str());
			set_scalar("x",x);
			set_constant("norb",norb);
				std::string tmp = job + "1.siox";
				const char* nm= tmp.c_str();
				add_sial_program(nm);
				std::string tmp1 = job + "2.siox";
				const char* nm1= tmp1.c_str();
				add_sial_program(nm1);
			int segs[]  = {2,3,4,1};
			set_aoindex_info(4,segs);
			finalize_setup();
		}

		//read and print setup_file
		setup::BinaryInputFile setup_file(job + ".dat");
		setup::SetupReader setup_reader(setup_file);
		std::cout << "SETUP READER DATA:\n" << setup_reader<< std::endl;

		sip::PersistentArrayManager<sip::Block,sip::Interpreter>* wpam;
		sip::PersistentArrayManager<sip::ServerBlock,sip::SIPServer>* spam;
		if (sip_mpi_attr.is_server())
			spam = new sip::PersistentArrayManager<sip::ServerBlock,sip::SIPServer>();
		else
		    wpam = new sip::PersistentArrayManager<sip::Block,sip::Interpreter>();


		//Execute first program
		//get siox name from setup, load and print the sip tables
		std::string prog_name = setup_reader.sial_prog_list_.at(0);
		std::string siox_dir(dir_name);
		setup::BinaryInputFile siox_file(siox_dir + prog_name);
		sip::SipTables sipTables(setup_reader, siox_file);
		if (!sip_mpi_attr.is_server()) {std::cout << "SIP TABLES for " << prog_name << '\n' << sipTables << std::endl;}

		//create worker and server
		sip::DataDistribution data_distribution(sipTables, sip_mpi_attr);

		if (sip_mpi_attr.global_rank()==0){   std::cout << "\n\n\n\n>>>>>>>>>>>>starting SIAL PROGRAM  "<< job << std::endl;}

		std::cout << "rank " << my_rank << " reached first barrier in test" << std::endl << std::flush;
		MPI_Barrier(MPI_COMM_WORLD);
		std::cout << "rank " << my_rank << " passed first barrier in test" << std::endl << std::flush;

		if (sip_mpi_attr.is_server()){
			sip::SIPServer server(sipTables, data_distribution, sip_mpi_attr, spam);
			std::cout << "reached prog 1 server barrier" << std::endl << std::flush;
			MPI_Barrier(MPI_COMM_WORLD);
			std::cout<<"passed prog 1 server barrier" << std::endl;
			server.run();
			spam->save_marked_arrays(&server);
		} else {
			sip::SialxTimer sialxTimer(sipTables.max_timer_slots());
			sip::Interpreter runner(sipTables, sialxTimer, wpam);
			std::cout << "reached prg 1 worker barrier " << std::endl << std::flush;
			MPI_Barrier(MPI_COMM_WORLD);
			std::cout << "passed prog 1 worker barrier "  << std::endl << std::flush;
			runner.interpret();
			wpam->save_marked_arrays(&runner);
		}

	   std::cout << std::flush;
	   if (sip_mpi_attr.global_rank()==0){
        std::cout << "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@" << std::endl << std::flush;
		std::cout << "SETUP READER DATA FOR SECOND PROGRAM:\n" << setup_reader<< std::endl;
	   }

			std::string prog_name2 = setup_reader.sial_prog_list_.at(1);
			setup::BinaryInputFile siox_file2(siox_dir + prog_name2);
			sip::SipTables sipTables2(setup_reader, siox_file2);
			if (sip_mpi_attr.global_rank()==0){
				std::cout << "SIP TABLES FOR " << prog_name2 << '\n' << sipTables2 << std::endl;
			}
			sip::DataDistribution data_distribution2(sipTables2, sip_mpi_attr);
			std::cout << "rank " << my_rank << " reached second barrier in test" << std::endl << std::flush;
			MPI_Barrier(MPI_COMM_WORLD);
			std::cout << "rank " << my_rank << " passed second barrier in test" << std::endl << std::flush;

			if (sip_mpi_attr.is_server()){
				sip::SIPServer server(sipTables2, data_distribution2, sip_mpi_attr, spam);
				std::cout << "reached prog 2 server barrier " << std::endl << std::flush;
				MPI_Barrier(MPI_COMM_WORLD);
				std::cout<<"passed prog 2 server barrier" << std::endl<< std::flush;
				server.run();
				spam->save_marked_arrays(&server);
			} else {
				sip::SialxTimer sialxTimers(sipTables2.max_timer_slots());
				sip::Interpreter runner(sipTables2, sialxTimers, wpam);
				std::cout << "reached prog 2 worker barrier " << std::endl << std::flush;
				MPI_Barrier(MPI_COMM_WORLD);
				std::cout << "passed prog 2 worker barrier"<< job  << std::endl<< std::flush;
				runner.interpret();
				wpam->save_marked_arrays(&runner);
				std::cout << "\nSIAL PROGRAM 2 TERMINATED"<< std::endl;
				//list_block_map();
			}

			std::cout << "rank " << my_rank << " reached third barrier in test" << std::endl << std::flush;
			MPI_Barrier(MPI_COMM_WORLD);
			std::cout << "rank " << my_rank << " passed third barrier in test" << std::endl << std::flush;

}
TEST(Sial,DISABLED_persistent_distributed_array_mpi){
//	std::cout << "****************************************\n";
//	sip::DataManager::scope_count=0;
//	//create setup_file
//	std::string job("persistent_distributed_array_test");
//	std::cout << "JOBNAME = " << job << std::endl;
//	double x = 3.456;
//	int norb = 2;
//
//	{	init_setup(job.c_str());
//		set_scalar("x",x);
//		set_constant("norb",norb);
//		std::string tmp = job + "1.siox";
//		const char* nm= tmp.c_str();
//		add_sial_program(nm);
//		std::string tmp1 = job + "2.siox";
//		const char* nm1= tmp1.c_str();
//		add_sial_program(nm1);
//		int segs[]  = {8,16};
//		set_aoindex_info(2,segs);
//		finalize_setup();
//	}
//
//	//read and print setup_file
//	//// setup::SetupReader setup_reader;
//
//	setup::BinaryInputFile setup_file(job + ".dat");
//	// setup_reader.read(setup_file);
//	setup::SetupReader setup_reader(setup_file);
//
//	std::cout << "SETUP READER DATA:\n" << setup_reader<< std::endl;
//	sip::PersistentArrayManager<sip::Block,sip::Interpreter>* pbm;
//	pbm = new sip::PersistentArrayManager<sip::Block,sip::Interpreter>();
//	//get siox name from setup, load and print the sip tables
//	std::string prog_name = setup_reader.sial_prog_list_.at(0);
//	std::string siox_dir(dir_name);
//	setup::BinaryInputFile siox_file(siox_dir + prog_name);
//	// sip::SioxReader siox_reader(&sipTables, &siox_file, &setup_reader);
////	siox_reader.read();
//	sip::SipTables sipTables(setup_reader, siox_file);
//	std::cout << "SIP TABLES" << '\n' << sipTables << std::endl;
//
//
//	//interpret the program
//	{
//	sip::SialxTimer sialxTimer(sipTables.max_timer_slots());
//
//	sip::Interpreter runner(sipTables, sialxTimer, pbm);
//	std::cout << "SIAL PROGRAM OUTPUT" << std::endl;
//	runner.interpret();
//	ASSERT_EQ(0, sip::DataManager::scope_count);
//	pbm->save_marked_arrays(&runner);
//	std::cout << "\nSIAL PROGRAM TERMINATED"<< std::endl;
////	runner.print_block()
//;
//	}
//	//get siox name from setup, load and print the sip tables
//	std::string prog_name2 = setup_reader.sial_prog_list_.at(1);
//	setup::BinaryInputFile siox_file2(siox_dir + prog_name2);
//	// sip::SioxReader siox_reader(&sipTables, &siox_file, &setup_reader);
////	siox_reader.read();
//	sip::SipTables sipTables2(setup_reader, siox_file2);
//	std::cout << "SIP TABLES FOR PROGRAM 2" << '\n' << sipTables2 << std::endl;
//
//
//	//interpret the program
//	{
//
//	sip::SialxTimer sialxTimer2(sipTables2.max_timer_slots());
//
//	sip::Interpreter runner2(sipTables2, sialxTimer2, pbm);
//	std::cout << "SIAL PROGRAM OUTPUT" << std::endl;
//	runner2.interpret();
//	ASSERT_EQ(0, sip::DataManager::scope_count);
//
//	std::cout << "\nSIAL PROGRAM TERMINATED"<< std::endl;
////	runner.print_block()
//	}
//	//delete pbm;

	sip::SIPMPIAttr &sip_mpi_attr = sip::SIPMPIAttr::get_instance();
	int my_rank = sip_mpi_attr.global_rank();

		std::cout << "****************************************\n";
		sip::DataManager::scope_count=0;
		//create setup_file
		std::string job("persistent_distributed_array_mpi");
		std::cout << "JOBNAME = " << job << std::endl;
		double x = 3.456;
		int norb = 4;

		{	init_setup(job.c_str());
			set_scalar("x",x);
			set_constant("norb",norb);
				std::string tmp = job + "1.siox";
				const char* nm= tmp.c_str();
				add_sial_program(nm);
				std::string tmp1 = job + "2.siox";
				const char* nm1= tmp1.c_str();
				add_sial_program(nm1);
			int segs[]  = {2,3,4,1};
			set_aoindex_info(4,segs);
			finalize_setup();
		}

		//read and print setup_file
		//// setup::SetupReader setup_reader;

		setup::BinaryInputFile setup_file(job + ".dat");
		// setup_reader.read(setup_file);
		setup::SetupReader setup_reader(setup_file);

		std::cout << "SETUP READER DATA:\n" << setup_reader<< std::endl;


		//get siox name from setup, load and print the sip tables
		std::string prog_name = setup_reader.sial_prog_list_.at(0);
		std::string siox_dir(dir_name);
		setup::BinaryInputFile siox_file(siox_dir + prog_name);
		// sip::SioxReader siox_reader(&sipTables, &siox_file, &setup_reader);
	//	siox_reader.read();
		sip::SipTables sipTables(setup_reader, siox_file);
//		if (!sip_mpi_attr.is_server()) {std::cout << "SIP TABLES" << '\n' << sipTables << std::endl;}

		//create worker and server

		if (sip_mpi_attr.global_rank()==0){   std::cout << "\n\n\n\n>>>>>>>>>>>>starting SIAL PROGRAM  "<< job << std::endl;}


		sip::DataDistribution data_distribution(sipTables, sip_mpi_attr);
		sip::PersistentArrayManager<sip::Block,sip::Interpreter>* wpam;
		sip::PersistentArrayManager<sip::ServerBlock,sip::SIPServer>* spam;
		if (sip_mpi_attr.is_server())
			spam = new sip::PersistentArrayManager<sip::ServerBlock,sip::SIPServer>();
		else
		    wpam = new sip::PersistentArrayManager<sip::Block,sip::Interpreter>();


		std::cout << "rank " << my_rank << " reached first barrier" << std::endl << std::flush;
		MPI_Barrier(MPI_COMM_WORLD);
		std::cout << "rank " << my_rank << " passed first barrier" << std::endl << std::flush;

		if (sip_mpi_attr.is_server()){
			sip::SIPServer server(sipTables, data_distribution, sip_mpi_attr, spam);
			std::cout << "at first barrier in prog 1 at server" << std::endl << std::flush;
			MPI_Barrier(MPI_COMM_WORLD);
			std::cout<<"passed first barrier at server, starting server" << std::endl;
			server.run();
			spam->save_marked_arrays(&server);
			std::cout << "Server state after termination" << server << std::endl;
		} else {
			sip::SialxTimer sialxTimer(sipTables.max_timer_slots());
			sip::Interpreter runner(sipTables, sialxTimer,  wpam);
			std::cout << "at first barrier in prog 1 at worker" << std::endl << std::flush;
			MPI_Barrier(MPI_COMM_WORLD);
			std::cout << "after first barrier; starting worker for "<< job  << std::endl;
			runner.interpret();
			wpam->save_marked_arrays(&runner);
			std::cout << "\n end of prog1 at worker"<< std::endl;
			//list_block_map();
		}

	   std::cout << std::flush;
	   if (sip_mpi_attr.global_rank()==0){
       std::cout << "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@" << std::endl << std::flush;
		std::cout << "SETUP READER DATA FOR SECOND PROGRAM:\n" << setup_reader<< std::endl;
	   }

			std::string prog_name2 = setup_reader.sial_prog_list_.at(1);
			setup::BinaryInputFile siox_file2(siox_dir + prog_name2);
			sip::SipTables sipTables2(setup_reader, siox_file2);
//			if (sip_mpi_attr.global_rank()==0){
//				std::cout << "SIP TABLES FOR " << prog_name2 << '\n' << sipTables2 << std::endl;
//			}
			sip::DataDistribution data_distribution2(sipTables2, sip_mpi_attr);
			std::cout << "rank " << my_rank << " reached second barrier in test" << std::endl << std::flush;
			MPI_Barrier(MPI_COMM_WORLD);
			std::cout << "rank " << my_rank << " passed second barrier in test" << std::endl << std::flush;

			if (sip_mpi_attr.is_server()){
				sip::SIPServer server(sipTables2, data_distribution2, sip_mpi_attr, spam);
				std::cout << "barrier in prog 2 at server" << std::endl << std::flush;
				MPI_Barrier(MPI_COMM_WORLD);
				std::cout<< "rank " << my_rank << "starting server for prog 2" << std::endl;
				server.run();
				std::cout<< "rank " << my_rank  << "Server state after termination of prog2" << server << std::endl;
			} else {
				sip::SialxTimer sialxTimer2(sipTables2.max_timer_slots());
				sip::Interpreter runner(sipTables2, sialxTimer2,  wpam);
				std::cout << "rank " << my_rank << "barrier in prog 2 at worker" << std::endl << std::flush;
				MPI_Barrier(MPI_COMM_WORLD);
				std::cout << "rank " << my_rank << "starting worker for prog2"<< job  << std::endl;
				runner.interpret();
				std::cout << "\nSIAL PROGRAM 2 TERMINATED"<< std::endl;
				//list_block_map();
			}

			std::cout << "rank " << my_rank << " reached third barrier in test" << std::endl << std::flush;
			MPI_Barrier(MPI_COMM_WORLD);
			std::cout << "rank " << my_rank << " passed third barrier in test" << std::endl << std::flush;

}


/************************************************/

TEST(SimpleMPI,DISABLED_get_mpi){
	sip::SIPMPIAttr &sip_mpi_attr = sip::SIPMPIAttr::get_instance();

		std::cout << "****************************************\n";
		sip::DataManager::scope_count=0;
		//create setup_file
		std::string job("get_mpi");
		std::cout << "JOBNAME = " << job << std::endl;
		double x = 3.456;
		int norb = 4;

		{	init_setup(job.c_str());
			set_scalar("x",x);
			set_constant("norb",norb);
			std::string tmp = job + ".siox";
			const char* nm= tmp.c_str();
			add_sial_program(nm);
			int segs[]  = {2,3,4,1};
			set_aoindex_info(4,segs);
			finalize_setup();
		}

		//read and print setup_file
		//// setup::SetupReader setup_reader;

		setup::BinaryInputFile setup_file(job + ".dat");
		// setup_reader.read(setup_file);
		setup::SetupReader setup_reader(setup_file);

		std::cout << "SETUP READER DATA:\n" << setup_reader<< std::endl;


		//get siox name from setup, load and print the sip tables
		std::string prog_name = setup_reader.sial_prog_list_.at(0);
		std::string siox_dir(dir_name);
		setup::BinaryInputFile siox_file(siox_dir + prog_name);
		// sip::SioxReader siox_reader(&sipTables, &siox_file, &setup_reader);
	//	siox_reader.read();
		sip::SipTables sipTables(setup_reader, siox_file);
//		if (!sip_mpi_attr.is_server()) {std::cout << "SIP TABLES" << '\n' << sipTables << std::endl;}

		//create worker and server

		if (sip_mpi_attr.global_rank()==0){   std::cout << "\n\n\n\nstarting SIAL PROGRAM  "<< job << std::endl;}


		sip::DataDistribution data_distribution(sipTables, sip_mpi_attr);
		if (sip_mpi_attr.is_server()){
			sip::SIPServer server(sipTables, data_distribution, sip_mpi_attr, NULL);
			MPI_Barrier(MPI_COMM_WORLD);
			std::cout<<"starting server" << std::endl;
			server.run();
			std::cout << "Server state after termination" << server << std::endl;
		} else {
			sip::SialxTimer sialxTimer(sipTables.max_timer_slots());
			sip::Interpreter runner(sipTables, sialxTimer,  NULL);
			MPI_Barrier(MPI_COMM_WORLD);
			std::cout << "starting worker for "<< job  << std::endl;
			runner.interpret();
			std::cout << "\nSIAL PROGRAM TERMINATED"<< std::endl;
			list_block_map();
		}


}

/************************************************/

TEST(SimpleMPI,unmatched_get){
	sip::SIPMPIAttr &sip_mpi_attr = sip::SIPMPIAttr::get_instance();

		std::cout << "****************************************\n";
		sip::DataManager::scope_count=0;
		//create setup_file
		std::string job("unmatched_get");
		std::cout << "JOBNAME = " << job << std::endl;
		double x = 3.456;
		int norb = 4;

		{	init_setup(job.c_str());
			set_scalar("x",x);
			set_constant("norb",norb);
			std::string tmp = job + ".siox";
			const char* nm= tmp.c_str();
			add_sial_program(nm);
			int segs[]  = {2,3,4,1};
			set_aoindex_info(4,segs);
			finalize_setup();
		}

		//read and print setup_file
		//// setup::SetupReader setup_reader;

		setup::BinaryInputFile setup_file(job + ".dat");
		// setup_reader.read(setup_file);
		setup::SetupReader setup_reader(setup_file);

		std::cout << "SETUP READER DATA:\n" << setup_reader<< std::endl;


		//get siox name from setup, load and print the sip tables
		std::string prog_name = setup_reader.sial_prog_list_.at(0);
		std::string siox_dir(dir_name);
		setup::BinaryInputFile siox_file(siox_dir + prog_name);
		// sip::SioxReader siox_reader(&sipTables, &siox_file, &setup_reader);
	//	siox_reader.read();
		sip::SipTables sipTables(setup_reader, siox_file);
//		if (!sip_mpi_attr.is_server()) {std::cout << "SIP TABLES" << '\n' << sipTables << std::endl;}

		//create worker and server

		if (sip_mpi_attr.global_rank()==0){   std::cout << "\n\n\n\nstarting SIAL PROGRAM  "<< job << std::endl;}


		sip::DataDistribution data_distribution(sipTables, sip_mpi_attr);
		if (sip_mpi_attr.is_server()){
			sip::SIPServer server(sipTables, data_distribution, sip_mpi_attr, NULL);
			MPI_Barrier(MPI_COMM_WORLD);
			std::cout<<"starting server" << std::endl;
			server.run();
			std::cout << "Server state after termination" << server << std::endl;
		} else {
			sip::SialxTimer sialxTimer(sipTables.max_timer_slots());
			sip::Interpreter runner(sipTables, sialxTimer,  NULL);
			MPI_Barrier(MPI_COMM_WORLD);
			std::cout << "starting worker for "<< job  << std::endl;
			runner.interpret();
			std::cout << "\nSIAL PROGRAM TERMINATED"<< std::endl;
			list_block_map();
		}
		std::cout << "this test should have printed a warning about unmatched get";


}
TEST(SimpleMPI,DISABLED_delete_mpi){
	sip::SIPMPIAttr &sip_mpi_attr = sip::SIPMPIAttr::get_instance();

		std::cout << "****************************************\n";
		sip::DataManager::scope_count=0;
		//create setup_file
		std::string job("delete_mpi");
		std::cout << "JOBNAME = " << job << std::endl;
		double x = 3.456;
		int norb = 4;

		{	init_setup(job.c_str());
			set_scalar("x",x);
			set_constant("norb",norb);
			std::string tmp = job + ".siox";
			const char* nm= tmp.c_str();
			add_sial_program(nm);
			int segs[]  = {2,3,4,1};
			set_aoindex_info(4,segs);
			finalize_setup();
		}

		//read and print setup_file
		//// setup::SetupReader setup_reader;

		setup::BinaryInputFile setup_file(job + ".dat");
		// setup_reader.read(setup_file);
		setup::SetupReader setup_reader(setup_file);

		std::cout << "SETUP READER DATA:\n" << setup_reader<< std::endl;


		//get siox name from setup, load and print the sip tables
		std::string prog_name = setup_reader.sial_prog_list_.at(0);
		std::string siox_dir(dir_name);
		setup::BinaryInputFile siox_file(siox_dir + prog_name);
		// sip::SioxReader siox_reader(&sipTables, &siox_file, &setup_reader);
	//	siox_reader.read();
		sip::SipTables sipTables(setup_reader, siox_file);
//		if (!sip_mpi_attr.is_server()) {std::cout << "SIP TABLES" << '\n' << sipTables << std::endl;}

		//create worker and server

		if (sip_mpi_attr.global_rank()==0){   std::cout << "\n\n\n\nstarting SIAL PROGRAM  "<< job << std::endl;}


		sip::DataDistribution data_distribution(sipTables, sip_mpi_attr);
		if (sip_mpi_attr.is_server()){
			sip::SIPServer server(sipTables, data_distribution, sip_mpi_attr, NULL);
			MPI_Barrier(MPI_COMM_WORLD);
			std::cout<<"starting server" << std::endl;
			server.run();

			std::cout << "Server state after termination" << server << std::endl;


		} else {
			sip::SialxTimer sialxTimer(sipTables.max_timer_slots());
			sip::Interpreter runner(sipTables, sialxTimer, NULL);
			MPI_Barrier(MPI_COMM_WORLD);
			std::cout << "starting worker for "<< job  << std::endl;
			runner.interpret();

//			std::cout << "Worker state after termination" << runner.data_manager_ << std::endl;
			std::cout << "\nSIAL PROGRAM TERMINATED"<< std::endl;
			//std::cout<<"PBM after program " << sialfpath << " :"<<std::endl<<pbm;

//			std::cout << "worker block map\n" << runner.data_manager_.block_manager_.block_map_ << std::endl;

		}


}
TEST(SimpleMPI,DISABLED_put_accumulate_mpi){
	sip::SIPMPIAttr &sip_mpi_attr = sip::SIPMPIAttr::get_instance();

		std::cout << "****************************************\n";
		sip::DataManager::scope_count=0;
		//create setup_file
		std::string job("put_accumulate_mpi");
		std::cout << "JOBNAME = " << job << std::endl;
		double x = 3.456;
		int norb = 4;

		{	init_setup(job.c_str());
			set_scalar("x",x);
			set_constant("norb",norb);
			std::string tmp = job + ".siox";
			const char* nm= tmp.c_str();
			add_sial_program(nm);
			int segs[]  = {2,3,4,1};
			set_aoindex_info(4,segs);
			finalize_setup();
		}

		//read and print setup_file
		//// setup::SetupReader setup_reader;

		setup::BinaryInputFile setup_file(job + ".dat");
		// setup_reader.read(setup_file);
		setup::SetupReader setup_reader(setup_file);

		std::cout << "SETUP READER DATA:\n" << setup_reader<< std::endl;


		//get siox name from setup, load and print the sip tables
		std::string prog_name = setup_reader.sial_prog_list_.at(0);
		std::string siox_dir(dir_name);
		setup::BinaryInputFile siox_file(siox_dir + prog_name);
		// sip::SioxReader siox_reader(&sipTables, &siox_file, &setup_reader);
	//	siox_reader.read();
		sip::SipTables sipTables(setup_reader, siox_file);
//		if (!sip_mpi_attr.is_server()) {std::cout << "SIP TABLES" << '\n' << sipTables << std::endl;}

		//create worker and server

		if (sip_mpi_attr.global_rank()==0){   std::cout << "\n\n\n\nstarting SIAL PROGRAM  "<< job << std::endl;}


		sip::DataDistribution data_distribution(sipTables, sip_mpi_attr);
		if (sip_mpi_attr.is_server()){
			sip::SIPServer server(sipTables, data_distribution, sip_mpi_attr, NULL);
			MPI_Barrier(MPI_COMM_WORLD);
			std::cout<<"starting server" << std::endl;
			server.run();

			std::cout << "Server state after termination" << server << std::endl;


		} else {
			sip::SialxTimer sialxTimer(sipTables.max_timer_slots());
			sip::Interpreter runner(sipTables, sialxTimer,  NULL);
			MPI_Barrier(MPI_COMM_WORLD);
			std::cout << "starting worker for "<< job  << std::endl;
			runner.interpret();

//			std::cout << "Worker state after termination" << runner.data_manager_ << std::endl;
			std::cout << "\nSIAL PROGRAM TERMINATED"<< std::endl;
			//std::cout<<"PBM after program " << sialfpath << " :"<<std::endl<<pbm;

//			std::cout << "worker block map\n" << runner.data_manager_.block_manager_.block_map_ << std::endl;

		}


}

TEST(SimpleMPI,DISABLED_put_test_mpi){
	sip::SIPMPIAttr &sip_mpi_attr = sip::SIPMPIAttr::get_instance();

		std::cout << "****************************************\n";
		sip::DataManager::scope_count=0;
		//create setup_file
		std::string job("put_test_mpi");
		std::cout << "JOBNAME = " << job << std::endl;
		double x = 3.456;
		int norb = 4;

		{	init_setup(job.c_str());
			set_scalar("x",x);
			set_constant("norb",norb);
			std::string tmp = job + ".siox";
			const char* nm= tmp.c_str();
			add_sial_program(nm);
			int segs[]  = {2,3,4,1};
			set_aoindex_info(4,segs);
			finalize_setup();
		}

		//read and print setup_file
		//// setup::SetupReader setup_reader;

		setup::BinaryInputFile setup_file(job + ".dat");
		// setup_reader.read(setup_file);
		setup::SetupReader setup_reader(setup_file);

		std::cout << "SETUP READER DATA:\n" << setup_reader<< std::endl;


		//get siox name from setup, load and print the sip tables
		std::string prog_name = setup_reader.sial_prog_list_.at(0);
		std::string siox_dir(dir_name);
		setup::BinaryInputFile siox_file(siox_dir + prog_name);
		// sip::SioxReader siox_reader(&sipTables, &siox_file, &setup_reader);
	//	siox_reader.read();
		sip::SipTables sipTables(setup_reader, siox_file);
//		if (!sip_mpi_attr.is_server()) {std::cout << "SIP TABLES" << '\n' << sipTables << std::endl;}

		//create worker and server

		if (sip_mpi_attr.global_rank()==0){   std::cout << "\n\n\n\nstarting SIAL PROGRAM  "<< job << std::endl;}


		sip::DataDistribution data_distribution(sipTables, sip_mpi_attr);
		if (sip_mpi_attr.is_server()){
			sip::SIPServer server(sipTables, data_distribution, sip_mpi_attr, NULL);
			MPI_Barrier(MPI_COMM_WORLD);
			std::cout<<"starting server" << std::endl;
			server.run();

			std::cout << "Server state after termination" << server << std::endl;


		} else {
			sip::SialxTimer sialxTimer(sipTables.max_timer_slots());
			sip::Interpreter runner(sipTables, sialxTimer,  NULL);
			MPI_Barrier(MPI_COMM_WORLD);
			std::cout << "starting worker for "<< job  << std::endl;
			runner.interpret();

//			std::cout << "Worker state after termination" << runner.data_manager_ << std::endl;
			std::cout << "\nSIAL PROGRAM TERMINATED"<< std::endl;
			//std::cout<<"PBM after program " << sialfpath << " :"<<std::endl<<pbm;

//			std::cout << "worker block map\n" << runner.data_manager_.block_manager_.block_map_ << std::endl;

		}


}


int main(int argc, char **argv) {

	MPI_Init(&argc, &argv);

#ifdef HAVE_TAU
	TAU_PROFILE_SET_NODE(0);
	TAU_STATIC_PHASE_START("SIP Main");
#endif

	sip::check(sizeof(int) >= 4, "Size of integer should be 4 bytes or more");
	sip::check(sizeof(double) >= 8, "Size of double should be 8 bytes or more");
	sip::check(sizeof(long long) >= 8, "Size of long long should be 8 bytes or more");

	int num_procs;
	sip::SIPMPIUtils::check_err(MPI_Comm_size(MPI_COMM_WORLD, &num_procs));

	if (num_procs < 2){
		std::cerr<<"Please run this test with at least 2 mpi ranks"<<std::endl;
		return -1;
	}

	sip::SIPMPIUtils::set_error_handler();
	sip::SIPMPIAttr &sip_mpi_attr = sip::SIPMPIAttr::get_instance();


	printf("Running main() from master_test_main.cpp\n");
	testing::InitGoogleTest(&argc, argv);
	int result = RUN_ALL_TESTS();

#ifdef HAVE_TAU
	TAU_STATIC_PHASE_STOP("SIP Main");
#endif

	 MPI_Finalize();

	return result;

}
