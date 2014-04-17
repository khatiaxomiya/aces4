/*
 * data_distribution.cpp
 *
 *  Created on: Jan 16, 2014
 *      Author: njindal
 */

#include <data_distribution.h>

namespace sip {

DataDistribution::DataDistribution(SipTables& sip_tables, SIPMPIAttr& sip_mpi_attr):
		sip_tables_(sip_tables), sip_mpi_attr_(sip_mpi_attr) {
}


int DataDistribution::get_server_rank(const sip::BlockId& bid) const{
	int array_id = bid.array_id();
	int array_rank = sip_tables_.array_rank(array_id);

//	// Calculate total number of blocks
//	int num_blocks = 1;
//	for (int pos=0; pos<array_rank; pos++){
//		int index_slot = sip_tables_.selectors(array_id)[pos];
//		int num_segments = sip_tables_.num_segments(index_slot);
//		num_blocks *= num_segments;
//	}

	//int num_blocks = sip_tables_.num_block_in_array(array_id);

	// Convert rank-dimensional index to 1-dimensional index
	int block_num = 0;
	int tmp = 1;
	for (int pos=array_rank-1; pos>=0; pos--){
		int index_slot = sip_tables_.selectors(array_id)[pos];
		int num_segments = sip_tables_.num_segments(index_slot);
		block_num += bid.index_values(pos) * tmp;
		tmp *= num_segments;
	}

	// Cyclic distribution
	int num_servers = sip_mpi_attr_.num_servers();
	int server_rank = block_num % num_servers;

	const std::vector<int> &server_ranks = sip_mpi_attr_.server_ranks();

	return server_ranks[server_rank];
}



} /* namespace sip */
