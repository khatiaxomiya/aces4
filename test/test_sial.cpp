/** This test case exercises sial language features that
 * are not local.
 */

#include "gtest/gtest.h"
#include <fenv.h>
#include <execinfo.h>
#include <signal.h>
#include <cstdlib>
#include <cassert>
#include "siox_reader.h"
#include "io_utils.h"
#include "setup_reader.h"

#include "sip_tables.h"
#include "interpreter.h"
#include "setup_interface.h"
#include "sip_interface.h"
#include "data_manager.h"
#include "global_state.h"
#include "sial_printer.h"

#include "worker_persistent_array_manager.h"

#include "block.h"

#ifdef HAVE_TAU
#include <TAU.h>
#endif

#ifdef HAVE_MPI
#include "sip_server.h"
#include "server_persistent_array_manager.h"
//#include "sip_mpi_attr.h"
//#include "global_state.h"
//#include "sip_mpi_utils.h"
//#else
//#include "sip_attr.h"
#endif

#include "test_constants.h"
#include "test_controller.h"
#include "test_controller_parallel.h"

extern "C" {
int test_transpose_op(double*);
int test_transpose4d_op(double*);
int test_contraction_small2(double*);
}

//bool VERBOSE_TEST = false;
bool VERBOSE_TEST = true;


TEST(Sial,empty){
	std::string job("empty");
	int norb = 3;
        int segs[] = {2,2,2};
	if (attr->global_rank() == 0) {
		init_setup(job.c_str());
		std::string tmp = job + ".siox";
		const char* nm = tmp.c_str();
		add_sial_program(nm);
		set_aoindex_info(3, segs);
		finalize_setup();
	}
	std::stringstream output;

	TestControllerParallel controller(job, true, VERBOSE_TEST, "", output);
	controller.initSipTables();
	controller.run();
	std::cout << "rank " << attr->global_rank() << "done" << std::endl << std::flush;
	barrier();
}

/** This function takes lower and upper ranges of indices
 * and runs test programs with all dimensions of these indices.
 * These programs test that the proper number of iterations has
 * been executed.  Jobs are pardo_loop_1d, ..., pardo_loop_6d.
 *
 * A variety of tests can be generated by changing the input params.
 */

void basic_pardo_test(int max_dims, int lower[], int upper[],
		bool expect_success = true) {
	assert(max_dims <= 6);
	for (int num_dims = 1; num_dims <= 6; ++num_dims) {
		std::stringstream job_ss;
		job_ss << "pardo_loop_" << num_dims << "d";
		std::string job = job_ss.str();

		//total number of iters for this sial program
		int num_iters = 1;
		for (int j = 0; j < num_dims; ++j) {
			num_iters *= ((upper[j] - lower[j]) + 1);
		}

		//create .dat file
		if (attr->global_rank() == 0) {
			init_setup(job.c_str());
			//add values for upper and lower bounds
			for (int i = 0; i < num_dims; ++i) {
				std::stringstream lower_ss, upper_ss;
				lower_ss << "lower" << i;
				upper_ss << "upper" << i;
				set_constant(lower_ss.str().c_str(), lower[i]);
				set_constant(upper_ss.str().c_str(), upper[i]);
			}
			std::string tmp = job + ".siox";
			const char* nm = tmp.c_str();
			add_sial_program(nm);
			finalize_setup();
		}

		TestControllerParallel controller(job, true, VERBOSE_TEST,
				"This is a test of " + job, std::cout, expect_success);
		controller.initSipTables();
		controller.run();
		if (attr->global_rank() == 0) {
			double total = controller.worker_->scalar_value("total");
			if (VERBOSE_TEST) {
				std::cout << "num_iters=" << num_iters << ", total=" << total
						<< std::endl;
			}
			EXPECT_EQ(num_iters, int(total));
		}
	}
}

TEST(Sial,pardo_loop) {
	int MAX_DIMS = 6;
	int lower[] = { 3, 2, 4, 1, 99, -1 };
	int upper[] = { 7, 6, 5, 1, 101, 2 };
	basic_pardo_test(6, lower, upper);
}

TEST(Sial,pardo_loop_corner_case) {
	int MAX_DIMS = 6;
	int lower[] = { 1, 1, 1, 1, 1, 1 };
	int upper[] = { 1, 1, 1, 1, 1, 1 };
	basic_pardo_test(6, lower, upper);
}

///*This case should fail with a message "FATAL ERROR: Pardo loop index i5 has empty range at :26"
// * IN addition to the assert throw, the controller constructor, which is in basic_pardo_test needs
// * to be passed false it final parameter, This param has default true, so is omitted in  most tests.
// */
//TEST(Sial,pardo_loop_illegal_range) {
//	int MAX_DIMS = 6;
//	int lower[] = { 1, 1, 1, 1, 1, 2 };
//	int upper[] = { 1, 1, 1, 1, 1, 1 };
//	ASSERT_THROW(basic_pardo_test(6, lower, upper, false), std::logic_error);
//
//}

TEST(Sial,broadcast_static){
	{
	std::string job("broadcast_static");
	int norb = 3;
	int segs[] = {2,3,2};
	int root = 0;
	if (attr->global_rank() == 0) {
		init_setup(job.c_str());
		set_constant("norb", norb);
		set_constant("root", root);
		std::string tmp = job + ".siox";
		const char* nm = tmp.c_str();
		add_sial_program(nm);
		set_aoindex_info(3, segs);
		finalize_setup();
	}
	std::stringstream output;

	TestControllerParallel controller(job, true, VERBOSE_TEST, "", output);
	controller.initSipTables();
	controller.run();
	if (attr->is_worker()) {
	double * a = controller.static_array("a");
	int expected[] = {1, 2, 1, 2, 3, 1, 2,
			3, 4, 4, 5, 6, 3, 4,
			1, 2, 1, 2, 3, 1, 2,
			3, 4, 4, 5, 6, 3, 4,
			5, 6, 7, 8, 9, 5, 6,
			1, 2, 1, 2, 3, 1, 2,
			3, 4, 4, 5, 6, 3, 4};
	int side = 2+3+2; //size of one side, from seg sizes in segs array above
	int size = side*side;
	int i = 0;
	for (i; i < size; ++i){
		ASSERT_DOUBLE_EQ(expected[i], a[i]);
	}
}
    if (attr-> is_worker()){
    	controller.worker_->gather_and_print_statistics(std::cerr);
    	barrier();
    }
    else {
    	barrier();
    	controller.server_->gather_and_print_statistics(std::cerr);
    }

	}
	barrier();
	if (attr->is_worker()){
    std::cerr << "done with worker" << std::endl << std::flush;
    barrier();
	}
	else {
	    barrier();
	    std::cerr << "done with server" << std::endl << std::flush;
	}

}


TEST(Sial,put_test) {
	std::string job("put_test");
	int norb = 3;
	int segs[] = { 2, 3, 2 };
	if (attr->global_rank() == 0) {
		init_setup(job.c_str());
		set_constant("norb", norb);
		set_constant("norb_squared", norb * norb);
		std::string tmp = job + ".siox";
		const char* nm = tmp.c_str();
		add_sial_program(nm);
		set_aoindex_info(3, segs);
		finalize_setup();
	}
	std::stringstream output;

	TestControllerParallel controller(job, true, VERBOSE_TEST, "", output);
	controller.initSipTables();
	controller.run();
	if (attr->is_worker()) {
		EXPECT_TRUE(controller.worker_->all_stacks_empty());
		std::vector<int> index_vec;
		for (int i = 0; i < norb; ++i) {
			for (int j = 0; j < norb; ++j) {
				int k = (i * norb + j) + 1;
				index_vec.push_back(k);
				double * local_block = controller.local_block("result",
						index_vec);
				double value = local_block[0];
				double expected = k * k * segs[i] * segs[j];
				std::cout << "k,value= " << k << " " << value << std::endl;
				ASSERT_DOUBLE_EQ(expected, value);
				index_vec.clear();
			}
		}
	}
}

TEST(Sial,put_initialize) {
	std::string job("put_initialize");
	int norb = 3;
	int segs[] = { 2, 3, 2 };
	if (attr->global_rank() == 0) {
		init_setup(job.c_str());
		set_constant("norb", norb);
		set_constant("norb_squared", norb * norb);
		std::string tmp = job + ".siox";
		const char* nm = tmp.c_str();
		add_sial_program(nm);
		set_aoindex_info(3, segs);
		finalize_setup();
	}
	std::stringstream output;

	TestControllerParallel controller(job, true, VERBOSE_TEST, "", output);
	controller.initSipTables();
	controller.run();
	if (attr->is_worker()) {
		EXPECT_TRUE(controller.worker_->all_stacks_empty());
		std::vector<int> index_vec;
		for (int i = 0; i < norb; ++i) {
			for (int j = 0; j < norb; ++j) {
				int k = (i * norb + j) + 1;
				index_vec.push_back(k);
				double * local_block = controller.local_block("result",
						index_vec);
				double value = local_block[0];
				double expected = k * k * segs[i] * segs[j];
				std::cout << "k,value= " << k << " " << value << std::endl;
				ASSERT_DOUBLE_EQ(expected, value);
				index_vec.clear();
			}
		}
	}

}

TEST(Sial,put_increment) {
	std::string job("put_increment");
	int norb = 3;
	int segs[] = { 2, 3, 2 };
	if (attr->global_rank() == 0) {
		init_setup(job.c_str());
		set_constant("norb", norb);
		set_constant("norb_squared", norb * norb);
		std::string tmp = job + ".siox";
		const char* nm = tmp.c_str();
		add_sial_program(nm);
		set_aoindex_info(3, segs);
		finalize_setup();
	}
	std::stringstream output;

	TestControllerParallel controller(job, true, VERBOSE_TEST, "", output);
	controller.initSipTables();
	controller.run();
//	if (attr->is_worker()) {
//		EXPECT_TRUE(controller.worker_->all_stacks_empty());
//		std::vector<int> index_vec;
//		for (int i = 0; i < norb; ++i) {
//			for (int j = 0; j < norb; ++j) {
//				int k = (i * norb + j) + 1;
//				index_vec.push_back(k);
//				double * local_block = controller.local_block("result",
//						index_vec);
//				double value = local_block[0];
//				double expected = k * k * segs[i] * segs[j];
//				std::cout << "k,value= " << k << " " << value << std::endl;
////				ASSERT_DOUBLE_EQ(expected, value);
//				index_vec.clear();
//			}
//		}
//	}

}
//TODO  restore functionality for single node version.  Was lost when PersistentArrayManager.h was refactored into
//worker and server versions.

TEST(Sial,persistent_scalars) {
	std::string job("persistent_scalars");
	double x = 3.456;
	double y = -0.1;

	{
		init_setup(job.c_str());
		set_scalar("x", x);
		set_scalar("y", y);
		std::string tmp1 = job + "_1.siox";
		const char* nm1 = tmp1.c_str();
		add_sial_program(nm1);
		std::string tmp2 = job + "_2.siox";
		const char* nm2 = tmp2.c_str();
		add_sial_program(nm2);
		finalize_setup();
	}

	std::stringstream output;
	TestControllerParallel controller(job, true, VERBOSE_TEST, "", output);
	controller.initSipTables();
	controller.run();
	if (attr->is_worker()) {
		ASSERT_DOUBLE_EQ(y, scalar_value("y"));
		ASSERT_DOUBLE_EQ(x, scalar_value("z"));
		ASSERT_DOUBLE_EQ(99.99, scalar_value("zz"));

		std::cout << "wpam:" << std::endl << *controller.wpam_ << std::endl
				<< "%%%%%%%%%%%%" << std::endl;
	}

	//Now do the second program
	//get siox name from setup, load and print the sip tables
	controller.initSipTables();
	controller.run();
	if (attr->is_worker()) {
		ASSERT_DOUBLE_EQ(x + 1, scalar_value("x"));
		ASSERT_DOUBLE_EQ(y, scalar_value("y"));
		ASSERT_DOUBLE_EQ(6, scalar_value("e"));
	}
}

TEST(Sial,get_mpi){
	std::string job("get_mpi");
	//create setup_file
	double x = 3.456;
	int norb = 4;
	int segs[]  = {2,3,4,1};

	if (attr->global_rank() == 0){
		init_setup(job.c_str());
		set_scalar("x",x);
		set_constant("norb",norb);
		std::string tmp = job + ".siox";
		const char* nm= tmp.c_str();
		add_sial_program(nm);
		set_aoindex_info(4,segs);
		finalize_setup();
	}

	std::stringstream output;
	TestControllerParallel controller(job, true, VERBOSE_TEST, "", output);
	controller.initSipTables();
	controller.run();

	if(attr->global_rank()==0){
		// Test a(1,1)
		// Get the data for local array block "b"
		int a_slot = controller.worker_->array_slot(std::string("a"));

		sip::index_selector_t a_indices_1;
		a_indices_1[0] = 1; a_indices_1[1] = 1;
		for (int i = 2; i < MAX_RANK; i++) a_indices_1[i] = sip::unused_index_value;
		sip::BlockId a_bid_1(a_slot, a_indices_1);
		std::cout << a_bid_1 << std::endl;
		sip::Block::BlockPtr a_bptr_1 = controller.worker_->get_block_for_reading(a_bid_1);
		sip::Block::dataPtr a_data_1 = a_bptr_1->get_data();
		std::cout << " Comparing block " << a_bid_1 << std::endl;
		for (int i=0; i<segs[0]; i++){
			for (int j=0; j<segs[0]; j++){
				ASSERT_DOUBLE_EQ(42*3, a_data_1[i*segs[0] + j]);
			}
		}
	}
}


//TEST(Sial,unmatched_get){
//	std::string job("unmatched_get");
//
//	double x = 3.456;
//	int norb = 4;
//
//	if (attr->global_rank() == 0){
//		init_setup(job.c_str());
//		set_scalar("x",x);
//		set_constant("norb",norb);
//		std::string tmp = job + ".siox";
//		const char* nm= tmp.c_str();
//		add_sial_program(nm);
//		int segs[]  = {2,3,4,1};
//		set_aoindex_info(4,segs);
//		finalize_setup();
//	}
//	std::stringstream output;
//	TestControllerParallel controller(job, true, VERBOSE_TEST, "This test should print a warning about unmatched get", output);
//	controller.initSipTables();
//	controller.run();
//}


TEST(Sial,delete_mpi){
	std::string job("delete_mpi");
	double x = 3.456;
	int norb = 4;

	if (attr->global_rank() == 0){
		init_setup(job.c_str());
		set_scalar("x",x);
		set_constant("norb",norb);
		std::string tmp = job + ".siox";
		const char* nm= tmp.c_str();
		add_sial_program(nm);
		int segs[]  = {2,3,4,1};
		set_aoindex_info(4,segs);
		finalize_setup();
	}

	std::stringstream output;
	TestControllerParallel controller(job, true, VERBOSE_TEST, "", output);
	controller.initSipTables();
	controller.run();
}


TEST(Sial,put_accumulate_mpi){
	std::string job("put_accumulate_mpi");
	double x = 3.456;
	int norb = 4;
	if (attr->global_rank() == 0){
		init_setup(job.c_str());
		set_scalar("x",x);
		set_constant("norb",norb);
		std::string tmp = job + ".siox";
		const char* nm= tmp.c_str();
		add_sial_program(nm);
		int segs[]  = {2,3,4,1};
		set_aoindex_info(4,segs);
		finalize_setup();
	}
	std::stringstream output;
	TestControllerParallel controller(job, true, VERBOSE_TEST, "", output);
	controller.initSipTables();
	controller.run();

}


/* TODO  check what this is testing.  It isn't clear that id doesn anyting */

TEST(Sial,all_rank_print){
	std::string job("all_rank_print_test");
	std::cout << "JOBNAME = " << job << std::endl;
	double x = 3.456;
	int norb = 2;
	if (attr->global_rank() == 0){
		init_setup(job.c_str());
		set_scalar("x",x);
		set_constant("norb",norb);
		std::string tmp = job + ".siox";
		const char* nm= tmp.c_str();
		add_sial_program(nm);
		int segs[]  = {2,3};
		set_aoindex_info(2,segs);
		finalize_setup();
	}

	std::stringstream output;
	TestControllerParallel controller(job, true, VERBOSE_TEST, "", output);
	controller.initSipTables();
	controller.run();
}


/* TODO check what this test does.  */
TEST(Sip,message_number_wraparound){

	std::string job("message_number_wraparound_test");

	if (attr->global_rank() == 0){
		init_setup(job.c_str());
		set_constant("norb",1);
		std::string tmp = job + ".siox";
		const char* nm= tmp.c_str();
		add_sial_program(nm);
		int segs[]  = {1};
		set_aoindex_info(1,segs);
		finalize_setup();
	}

	std::stringstream output;
	TestControllerParallel controller(job, true, VERBOSE_TEST, "", output);
	controller.initSipTables();
	controller.run();
}
/*
 * predefined int eom_roots
predefined int eom_subspc
index kstate   = 1: eom_roots
index ksub     = 1: eom_subspc
moaindex i = baocc: eaocc
moaindex i1= baocc: eaocc
moaindex a = bavirt: eavirt
moaindex a1= bavirt: eavirt

served RB2_aa[ksub,a,i,a1,i1]
served R1k2_aa[kstate,a,i,a1,i1]
contiguous local CLRB2_aa[ksub,a,i,a1,i1]
 */
TEST(Sial,contig_local3){
	std::string job("contig_local3");
	double x = 3.456;
	int norb = 8;
	int eom_roots = 4;
	int eom_subspc = 8;
	int baocc = 1;
	int eaocc = 3;
	int bavirt = 4;
	int eavirt = 8;
	if (attr->global_rank() == 0){
		init_setup(job.c_str());
		set_constant("eom_roots",eom_roots);
		set_constant("eom_subspc",8);
		set_constant("baocc",baocc);
		set_constant("eaocc",eaocc);
		set_constant("bavirt",bavirt);
		set_constant("eavirt",eavirt);
        set_constant("norb",norb);
		std::string tmp = job + ".siox";
		const char* nm= tmp.c_str();
		add_sial_program(nm);
		int segs[]  = {2,3,4,1,4,4,4,4};
		set_moaindex_info(8,segs);
		finalize_setup();
	}
	std::stringstream output;
	TestControllerParallel controller(job, true, VERBOSE_TEST, "", output);
	controller.initSipTables();
	controller.run();
	//TODO finish this
//#ifdef HAVE_MPI
//	if (attr->is_server_){
//		for (size_t kstate = 1; kstate < eom_roots; ++kstate){
//			for (size_t a = bavirt; a < eavirt ; ++a){
//				for (size_t i = baocc; i < eaocc : ++i){
//					for (size_t a1 = bavirt; a1 < eavirt; ++a1){
//						for (size_t i1 = baocc; i1 < eaocc; ++i1){
//							isFilled(kstate, controller.)
//						}
//					}
//				}
//			}
//		}
//	}
}

//bool isFilled(double value, sip::Block::BlockPtr block){
//	size_t size = block->size();
//	for (size_t i = 0; i < size; ++i){
//		ASSERT_DOUBLE_EQ(value, block->get_data()[i]);
//	}
//	return true;
//}

TEST(Sial,persistent_distributed_array_mpi){
	std::string job("persistent_distributed_array_mpi");
	double x = 3.456;
	int norb = 2;
	int segs[]  = {2,3};

	if (attr->global_rank() == 0){
		init_setup(job.c_str());
		set_scalar("x",x);
		set_constant("norb",norb);
		std::string tmp = job + "1.siox";
		const char* nm= tmp.c_str();
		add_sial_program(nm);
		std::string tmp1 = job + "2.siox";
		const char* nm1= tmp1.c_str();
		add_sial_program(nm1);
		set_aoindex_info(2,segs);
		finalize_setup();
	}


	std::stringstream output;
	TestControllerParallel controller(job, true, true, "", output);

	//run first program
	controller.initSipTables();
	controller.run();
	controller.print_timers(std::cout);
	std::cout << "Rank " << attr->global_rank() << " in persistent_distributed_array_mpi starting second program" << std::endl << std::flush;

	//run second program
	controller.initSipTables();
	controller.run();
	if (attr->is_worker()) {
		int i,j;
		for (i=1; i <= norb ; ++i ){
			for (j = 1; j <= norb; ++j){
			    double firstval = (i-1)*norb + j;
			    std::vector<int> indices;
			    indices.push_back(i);
			    indices.push_back(j);
			    double * block_data = controller.local_block(std::string("a"),indices);
			    size_t block_size = segs[i-1] * segs[j-1];
			    for (size_t count = 0; count < block_size; ++count){
			    	ASSERT_DOUBLE_EQ(3*firstval, block_data[count]);
			    	firstval++;
			    }
			}
		}
	}
//	controller.print_timers(std::cout);
    if (attr-> is_worker()){
    	controller.worker_->gather_and_print_statistics(std::cerr);
    	barrier();
    }
    else {
    	barrier();
    	controller.server_->gather_and_print_statistics(std::cerr);
    }
    barrier();

}

TEST(Sial,DISABLED_cached_block_map_test) {
    std::string job("cached_block_map_test");
    int norb = 4;
    int iterations = 3;
    int segs[] = { 26, 26, 26, 26 };
    if (attr->global_rank() == 0) {
        init_setup(job.c_str());
        set_constant("norb", norb);
        set_constant("iterations", iterations);
        std::string tmp = job + ".siox";
        const char* nm = tmp.c_str();
        add_sial_program(nm);
        set_aoindex_info(4, segs);
        finalize_setup();
    }
    std::stringstream output;

    std::size_t limit_size = 80 * 1024 * 1024; // 100 MB
    sip::GlobalState::set_max_server_data_memory_usage(limit_size);
    sip::GlobalState::set_max_worker_data_memory_usage(limit_size);
    TestControllerParallel controller(job, true, VERBOSE_TEST, "", output);
    controller.initSipTables();
    controller.run();
    if (attr->is_worker()) {
        EXPECT_TRUE(controller.worker_->all_stacks_empty());
    }
    sip::GlobalState::reinitialize();
}


TEST(Sial,DISABLED_cached_block_map_test_no_dangling_get) {
    std::string job("cached_block_map_test_no_dangling_get");
    int norb = 4;
    int iterations = 3;
    int segs[] = { 26, 26, 26, 26 };
    if (attr->global_rank() == 0) {
        init_setup(job.c_str());
        set_constant("norb", norb);
        set_constant("iterations", iterations);
        std::string tmp = job + ".siox";
        const char* nm = tmp.c_str();
        add_sial_program(nm);
        set_aoindex_info(4, segs);
        finalize_setup();
    }
    std::stringstream output;

    std::size_t limit_size = 80 * 1024 * 1024; // 100 MB
    sip::GlobalState::set_max_server_data_memory_usage(limit_size);
    sip::GlobalState::set_max_worker_data_memory_usage(limit_size);
    TestControllerParallel controller(job, true, VERBOSE_TEST, "", output);
    controller.initSipTables();
    controller.run();
    if (attr->is_worker()) {
        EXPECT_TRUE(controller.worker_->all_stacks_empty());
    }
    sip::GlobalState::reinitialize();
}



TEST(Sial,pardo_load_balance_test){
	// This test needs to be run with more than 1 worker.
	// It is NOT AUTOMATED
	// PLEASE EXAMINE THE OUTPUT MANUALLY
	std::string job("pardo_load_balance_test");
	int norb = 4;
	int segs[] = {2,3,2,2};
	if (attr->global_rank() == 0) {
		init_setup(job.c_str());
		set_constant("norb", norb);
		std::string tmp = job + ".siox";
		const char* nm = tmp.c_str();
		add_sial_program(nm);
		set_aoindex_info(4, segs);
		finalize_setup();
	}
	std::stringstream output;

	TestControllerParallel controller(job, true, VERBOSE_TEST, "", output);
	controller.initSipTables();
	controller.run();

}


TEST(Sial,pardo_with_where){
    std::string job("pardo_with_where");
    int norb = 4;
    int segs[] = {2,3,2,2};
    if (attr->global_rank() == 0) {
        init_setup(job.c_str());
        set_constant("norb", norb);
        std::string tmp = job + ".siox";
        const char* nm = tmp.c_str();
        add_sial_program(nm);
        set_aoindex_info(4, segs);
        finalize_setup();
    }
    std::stringstream output;

    TestControllerParallel controller(job, true, VERBOSE_TEST, "", output);
    controller.initSipTables();
    controller.run();
    if (attr-> is_worker()){
    	controller.worker_->gather_and_print_statistics(std::cerr);
    	barrier();
    }
    else {
    	barrier();
    	controller.server_->gather_and_print_statistics(std::cerr);
    }
    barrier();
}

TEST(Sial,put_accumulate_stress){
    std::string job("put_accumulate_stress");
    int norb = 4;
    int kmax = 20;
    int segs[] = {2,3,2,2};
    if (attr->global_rank() == 0) {
        init_setup(job.c_str());
        set_constant("norb", norb);
        set_constant("kmax", kmax);
        std::string tmp = job + ".siox";
        const char* nm = tmp.c_str();
        add_sial_program(nm);
        set_aoindex_info(4, segs);
        finalize_setup();
    }
    std::stringstream output;

    TestControllerParallel controller(job, true, VERBOSE_TEST, "", output);
    controller.initSipTables();
    controller.run();

//    std::cerr << "finished run, checking results" << std::endl << std::flush;

	if (attr->is_worker()) {
		int i,j;
		for (i=1; i <= norb ; ++i ){
			for (j = 1; j <= norb; ++j){
			    double value = kmax*(2*i + 2*j);
			    std::vector<int> indices;
			    indices.push_back(i);
			    indices.push_back(j);
			    double * block_data = controller.local_block(std::string("a"),indices);
			    size_t block_size = segs[i-1] * segs[j-1];
			    for (size_t count = 0; count < block_size; ++count){
//			    	if (count == 0){
//			    		std::cerr << "i=" << i << " j=" << j << " value="<< value << " block_data[0]=" << block_data[count] << std::endl << std::flush;
//			    	}
			    	ASSERT_DOUBLE_EQ(value, block_data[count]);
			    }
			}
		}
	}
    if (attr-> is_worker()){
     	controller.worker_->gather_and_print_statistics(std::cerr);
    	barrier();
    }
    else {
    	barrier();
    	controller.server_->gather_and_print_statistics(std::cerr);
    }
    barrier();
}

/* This test sets a very low limit for memory usage at the server
 * and sends enough blocks to require disk backing.
 *
 * Each array has 10 30x30x30x30 blocks (= 64,800,000 bytes)
 * This test creates 4 of them, sends them to a server, then
 * reads them back and gets the results.  The server limit is
 * set at 70,000,000, so disk backing should start with the second
 * array.
 */
TEST(Sip,disk_backing_test) {
	std::string job("disk_backing_test");
	size_t limit_size = 70000000;
    sip::GlobalState::set_max_server_data_memory_usage(limit_size);
    if ( attr->global_rank() == 0){
    std::cout << "worker memory limit " << sip::GlobalState::get_max_worker_data_memory_usage() << std::endl;
    std::cout << "server memory limit " << sip::GlobalState::get_max_server_data_memory_usage() << std::endl << std::flush;
    }
    barrier();
    int norb = 9;
	int segs[] = { 900, 900, 900, 900, 900, 900, 900, 900, 900 };
	if (attr->global_rank() == 0) {
		init_setup(job.c_str());
		set_constant("norb", norb);
		set_constant("norb_squared", norb * norb);
		std::string tmp = job + ".siox";
		const char* nm = tmp.c_str();
		add_sial_program(nm);
		set_aoindex_info(9, segs);
		finalize_setup();
	}
	std::stringstream output;

	TestControllerParallel controller(job, true, VERBOSE_TEST, "", output);
	controller.initSipTables();
	controller.run();
//	if (attr->is_worker()) {
//		EXPECT_TRUE(controller.worker_->all_stacks_empty());
//		std::vector<int> index_vec;
//		for (int i = 0; i < norb; ++i) {
//			for (int j = 0; j < norb; ++j) {
//				int k = (i * norb + j) + 1;
//				index_vec.push_back(k);
//				double * local_block = controller.local_block("result0",
//						index_vec);
//				double value = local_block[0];
//				double expected = k * k * segs[i] * segs[j];
//				std::cout << "k,value= " << k << " " << value << std::endl;
//				ASSERT_DOUBLE_EQ(expected, value);
//				index_vec.clear();
//			}
//		}
//	}

	barrier();
	std::cout << "global rank " << attr->global_rank() << "attr->is_worker()" << attr->is_worker();
	std::cout << std::endl << std::flush;
	if (attr->is_worker()) {
		controller.worker_->gather_and_print_statistics(std::cout);
		barrier();
	} else {
		barrier();
		controller.server_->gather_and_print_statistics(std::cout);
	}
	barrier();
    sip::GlobalState::reinitialize();
}

/* This test sets a very low limit for memory usage at the server
 * and sends enough blocks to require disk backing.
 *
 * It is the same as disk_backing_test except that it does some
 * put accumulates after the previous work.
 */
TEST(Sip,disk_backing_put_acc_stress) {
	std::string job("disk_backing_test");
	size_t limit_size = 70000000;
    sip::GlobalState::set_max_server_data_memory_usage(limit_size);
    if ( attr->global_rank() == 0){
    std::cout << "worker memory limit " << sip::GlobalState::get_max_worker_data_memory_usage() << std::endl;
    std::cout << "server memory limit " << sip::GlobalState::get_max_server_data_memory_usage() << std::endl << std::flush;
    }
    barrier();
    int norb = 9;
	int segs[] = { 900, 900, 900, 900, 900, 900, 900, 900, 900 };
	if (attr->global_rank() == 0) {
		init_setup(job.c_str());
		set_constant("norb", norb);
		set_constant("norb_squared", norb * norb);
		std::string tmp = job + ".siox";
		const char* nm = tmp.c_str();
		add_sial_program(nm);
		set_aoindex_info(9, segs);
		finalize_setup();
	}
	std::stringstream output;

	TestControllerParallel controller(job, true, VERBOSE_TEST, "", output);
	controller.initSipTables();
	controller.run();
//	if (attr->is_worker()) {
//		EXPECT_TRUE(controller.worker_->all_stacks_empty());
//		std::vector<int> index_vec;
//		for (int i = 0; i < norb; ++i) {
//			for (int j = 0; j < norb; ++j) {
//				int k = (i * norb + j) + 1;
//				index_vec.push_back(k);
//				double * local_block = controller.local_block("result0",
//						index_vec);
//				double value = local_block[0];
//				double expected = k * k * segs[i] * segs[j];
//				std::cout << "k,value= " << k << " " << value << std::endl;
//				ASSERT_DOUBLE_EQ(expected, value);
//				index_vec.clear();
//			}
//		}
//	}

	barrier();
	std::cout << "global rank " << attr->global_rank() << "attr->is_worker()" << attr->is_worker();
	std::cout << std::endl << std::flush;
	if (attr->is_worker()) {
		controller.worker_->gather_and_print_statistics(std::cerr);
		barrier();
	} else {
		barrier();
		controller.server_->gather_and_print_statistics(std::cerr);
	}
	barrier();
    sip::GlobalState::reinitialize();
	std::cerr << "at end of disk_backing_put_acc_stress" << std::endl << std::flush;
}

/* This test is the same as disk_backing_test except that the memory limit is the default.
 *
 * Each array has 10 30x30x30x30 blocks (= 64,800,000 bytes)
 * This test creates 4 of them, sends them to a server, then
 * reads them back and gets the results.
 */
TEST(Sip,disk_backing_test_default_limit) {
	std::string job("disk_backing_test_default_limit");
	std::string siox("disk_backing_test");
//	if (attr->is_worker()){
//		std::cout << "I am a worker with global rank " << attr->global_rank() << std::endl << std::flush;
//	}
//	else {
//		std::cout << "I am a server with global rank " << attr->global_rank() << std::endl << std::flush;
//	}
    if ( attr->global_rank() == 0){
    std::cout << "worker memory limit " << sip::GlobalState::get_max_worker_data_memory_usage() << std::endl;
    std::cout << "server memory limit " << sip::GlobalState::get_max_server_data_memory_usage() << std::endl << std::flush;
    }
    barrier();
    int norb = 9;
	int segs[] = { 900, 900, 900, 900, 900, 900, 900, 900, 900 };
	if (attr->global_rank() == 0) {
		init_setup(job.c_str());
		set_constant("norb", norb);
		set_constant("norb_squared", norb * norb);
		std::string tmp = siox + ".siox";
		const char* nm = tmp.c_str();
		add_sial_program(nm);
		set_aoindex_info(9, segs);
		finalize_setup();
	}
	std::stringstream output;

	TestControllerParallel controller(job, true, VERBOSE_TEST, "", output);
	controller.initSipTables();
	controller.run();
	if (attr->is_worker()) {
		EXPECT_TRUE(controller.worker_->all_stacks_empty());
		std::vector<int> index_vec;
		for (int i = 0; i < norb; ++i) {
			for (int j = 0; j < norb; ++j) {
				for (int kk = 0; kk < norb; ++kk) {
					if (kk == (i * norb + j)) {
						int k = kk + 1;
						index_vec.push_back(k);
						double * local_block = controller.local_block("result0",
								index_vec);
						double value = local_block[0];
						double expected = k * k * segs[i] * segs[j];
						std::cout << "k,value= " << k << " " << value
								<< std::endl;
						ASSERT_DOUBLE_EQ(expected, value);
						index_vec.clear();
					}
				}
			}
		}
	}
	barrier();

	std::cout << "global rank " << attr->global_rank() << " attr->is_worker() "
			<< attr->is_worker();
	std::cout << std::endl << std::flush;
	if (attr->is_server()) {
		controller.server_->gather_and_print_statistics(std::cout);
		std::cout << std::flush;
		barrier();
	} else {
		barrier();
		controller.worker_->gather_and_print_statistics(std::cout);
		std::cout << std::flush;
	}
	barrier();
	sip::GlobalState::reinitialize();
}
//****************************************************************************************************************

void bt_sighandler(int signum) {
    std::cerr << "Interrupt signal (" << signum << ") received." << std::endl;
    FAIL();
    abort();
}

int main(int argc, char **argv) {

    //    feenableexcept(FE_DIVBYZERO);
    //    feenableexcept(FE_OVERFLOW);
    //    feenableexcept(FE_INVALID);
    //
    //    signal(SIGSEGV, bt_sighandler);
    //    signal(SIGFPE, bt_sighandler);
    //    signal(SIGTERM, bt_sighandler);
    //    signal(SIGINT, bt_sighandler);
    //    signal(SIGABRT, bt_sighandler);

#ifdef HAVE_MPI
    MPI_Init(&argc, &argv);
    int num_procs;
    sip::SIPMPIUtils::check_err(MPI_Comm_size(MPI_COMM_WORLD, &num_procs), __LINE__,__FILE__);

    if (num_procs < 2) {
        std::cerr << "Please run this test with at least 2 mpi ranks"
            << std::endl;
        return -1;
    }
    sip::SIPMPIUtils::set_error_handler();
    sip::SIPMPIAttr &sip_mpi_attr = sip::SIPMPIAttr::get_instance();
    attr = &sip_mpi_attr;
#endif
    barrier();
#ifdef HAVE_TAU
    TAU_PROFILE_SET_NODE(0);
    TAU_STATIC_PHASE_START("SIP Main");
#endif

    //	sip::check(sizeof(int) >= 4, "Size of integer should be 4 bytes or more");
    //	sip::check(sizeof(double) >= 8, "Size of double should be 8 bytes or more");
    //	sip::check(sizeof(long long) >= 8, "Size of long long should be 8 bytes or more");
    //
    //	int num_procs;
    //	sip::SIPMPIUtils::check_err(MPI_Comm_size(MPI_COMM_WORLD, &num_procs));
    //
    //	if (num_procs < 2){
    //		std::cerr<<"Please run this test with at least 2 mpi ranks"<<std::endl;
    //		return -1;
    //	}
    //
    //	sip::SIPMPIUtils::set_error_handler();
    //	sip::SIPMPIAttr &sip_mpi_attr = sip::SIPMPIAttr::get_instance();
    //

    printf("Running main() from test_sial.cpp\n");
    testing::InitGoogleTest(&argc, argv);
    barrier();
    int result = RUN_ALL_TESTS();

    std::cout << "Rank  " << attr->global_rank() << " Finished RUN_ALL_TEST() " << std::endl << std::flush;

#ifdef HAVE_TAU
    TAU_STATIC_PHASE_STOP("SIP Main");
#endif
    barrier();
#ifdef HAVE_MPI
    MPI_Finalize();
#endif
    return result;
}
