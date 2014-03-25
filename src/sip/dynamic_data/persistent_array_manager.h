/*
 * persistent_array_manager.h
 *
 *  Created on: Jan 8, 2014
 *      Author: njindal
 */

#ifndef PERSISTENT_ARRAY_MANAGER_H_
#define PERSISTENT_ARRAY_MANAGER_H_

#include "mpi.h"

#include <vector>
//#include <set>

#include "blocks.h"
#include "block_manager.h"
#include "sip_mpi_attr.h"
#include "id_block_map.h"
#include "sip_tables.h"
#include "sip_server.h"
#include "interpreter.h"
#include "contiguous_array_manager.h"

namespace sip {

/**
 * Data structure used for persistent Scalars and arrays.
 *
 * In the SIAL program:
 *     set_persistent array1 "label1"
 * causes array1 with the string slot for the string literal to be saved
 * in the persistent_array_map_.  In the HAVE_MPI build, if array1 id distributed,
 * messages are sent to all servers to add the entry to the server's persistent_array_map_.
 *
 * After the sial program is finished, save_marked_arrays() is invoked at both workers
 * and servers.  The values of marked (i.e. those that are in the persistent_array_map_) scalars
 * are then copied into the scalar_value_map_ with the string corresponding to the
 * string_slot as key.  Marked contiguous arrays, and the block map for distributed
 * arrays are MOVED to this class's contiguous_array_map_ or distribute_array_map_
 * respectively. The string itself is used as a key because string slots are assigned
 * by the sial compiler and are only valid in a single sial program.
 *
 * restore_persisent_array copies the scalar value to the sip ScalarTable,
 * or MOVES the pointers to the contiguous array or block map for a distributed/served
 * array to the sip ContiguousArrayManager or BlockManager, respectively.
 *
 * A consequence is that  any object can only be restored once.  If it is needed again in subsequent
 * SIAL programs, set_persistent needs to be invoked again in the SIAL program.
 *
 * These semantics were chosen to allow clear ownership transfer of allocated memory without
 * unnecessary copying or garbage.
 */
template<typename BLOCK_TYPE>
class PersistentArrayManager {

public:

	/**
	 * Type of map for storing persistent contiguous arrays between SIAL programs.  Only used at workers
	 */
	typedef std::map<std::string, Block*> LabelContiguousArrayMap;
	/**
	 * Type of map for storing persistent distributed and served arrays between SIAL programs.
	 * In parallel implementation., only used at servers
	 */
	typedef std::map<std::string, typename IdBlockMap<BLOCK_TYPE>::PerArrayMap*> LabelDistributedArrayMap;
	/**
	 * Type of map for storing persistent scalars between SIAL programs.  Only used at workers
	 */
	typedef std::map<std::string, double> LabelScalarValueMap;

	/**
	 * Type of map for storing the array id and slot of label for scalars and arrays that have been marked persistent.
	 */
	typedef std::map<int, int> ArrayIdLabelMap;	// Map of arrays marked for persistence

	PersistentArrayManager() :
			sip_mpi_attr_(sip::SIPMPIAttr::get_instance()) {
	}

	~PersistentArrayManager() {
	}

	/** Called to implement SIAL set_persistent command
	 *
	 * @param array_id
	 * @param string_slot
	 */
	void set_persistent(int array_id, int string_slot) {
		std::pair<ArrayIdLabelMap::iterator, bool> ret =
				persistent_array_map_.insert(
						std::pair<int, int>(array_id, string_slot));
		check(ret.second,
				"duplicate save of array in same sial program "
						+ array_name_value(array_id));
		//duplicate label for same type of object will
		//be detected during the save process so we don't
		//check for that here.
	}


	/**
	 * Called during post processing.
	 * Values of marked scalars are copied into the scalar_value_map_;
	 * marked contiguous and distributed/served arrays are MOVED to
	 * the contiguous_array_map_ and distributed_array_map_ respectively.
	 *
	 * Note that in a  parallel implementation, distributed arrays
	 *  should only be marked at servers. Scalars and contiguous arrays are
	 *  only at workers.
	 */
	void save_marked_arrays_on_worker(Interpreter* worker) {
		SipTables& sip_tables = worker->sip_tables_;
		ArrayIdLabelMap::iterator it;
		for (it = persistent_array_map_.begin();
				it != persistent_array_map_.end(); ++it) {
			int array_id = it->first;
			int string_slot = it->second;
			const std::string label = sip_tables.string_literal(string_slot);
			if (sip_tables.is_scalar(array_id)) {
				double value = worker->scalar_value(array_id);
				save_scalar(label, value);
			} else if (sip_tables.is_contiguous(array_id)) {
				Block* contiguous_array =
						worker->get_and_remove_contiguous_array(array_id);
				save_contiguous(label, contiguous_array);
			} else {
				//in parallel implementation, there won't be any of these on worker.
				IdBlockMap<Block>::PerArrayMap* per_array_map =
						worker->get_and_remove_per_array_map(array_id);
			save_distributed(label, per_array_map);
			}
		}
		persistent_array_map_.clear();
	}

	void save_marked_arrays_on_server(SIPServer* server) {
		SipTables& sip_tables = server->sip_tables();
		ArrayIdLabelMap::iterator it;
		for (it = persistent_array_map_.begin();
				it != persistent_array_map_.end(); ++it) {
			int array_id = it->first;
			int string_slot = it->second;
			const std::string label = sip_tables.string_literal(string_slot);
			IdBlockMap<ServerBlock>::PerArrayMap* per_array_map =
					server->get_and_remove_per_array_map(array_id);
			save_distributed(label, per_array_map);

		}
		persistent_array_map_.clear();
	}

	void restore_persistent(Interpreter* worker, int array_id,
			int string_slot){
		if (worker->is_scalar(array_id))
			restore_persistent_scalar(worker, array_id, string_slot);
		else if (worker->is_contiguous(array_id))
			restore_persistent_contiguous(worker, array_id, string_slot);
		else //should only happen in sequential
			restore_persistent_distributed_worker(worker, array_id, string_slot);
	}

	/** Invoked by worker to implement restore_persistent command in
	 * SIAl when the argument is a scalar.  The value associated with the
	 * string literal is copied into the scalar table and the entry removed
	 * from the persistent_array_manager.
	 *
	 * @param worker
	 * @param array_id
	 * @param string_slot
	 */
	void restore_persistent_scalar(Interpreter* worker, int array_id,
			int string_slot) {
		std::string label = worker->sip_tables_.string_literal(string_slot);
		LabelScalarValueMap::iterator it = scalar_value_map_.find(label);
		check(it != scalar_value_map_.end(),
				"scalar to restore with label " + label + " not found");
		worker->set_scalar_value(array_id, it->second);
		scalar_value_map_.erase(it);
	}

	/** Invoked by worker to implement restore_persistent command in
	 * SIAl when the argument is a contiguous array.  The array associated
	 * with the string literal is MOVED into the scalar table and the
	 * entry removed (which does not delete the block) from the
	 * persistent_array_manager.
	 *
	 * @param worker
	 * @param array_id
	 * @param string_slot
	 */
	void restore_persistent_contiguous(Interpreter* worker, int array_id,
			int string_slot) {
		std::string label = worker->sip_tables_.string_literal(string_slot);
		LabelContiguousArrayMap::iterator it = contiguous_array_map_.find(
				label);
		check(it != contiguous_array_map_.end(),
				"contiguous array to restore with label " + label
						+ " not found");
		worker->set_contiguous_array(array_id, it->second);
		contiguous_array_map_.erase(it);
	}

	void restore_persistent_distributed_worker(Interpreter* worker,
			int array_id, int string_slot) {
		std::string label = worker->sip_tables_.string_literal(string_slot);
		typename LabelDistributedArrayMap::iterator it = distributed_array_map_.find(
				label);
		check(it != distributed_array_map_.end(),
				"distributed/served array to restore with label " + label
						+ " not found");
		worker->set_per_array_map(array_id, it->second);
		distributed_array_map_.erase(it);
	}

	void restore_persistent_distributed_server(SIPServer* worker,
			int array_id, int string_slot) {
		std::string label = worker->sip_tables().string_literal(string_slot);
		typename LabelDistributedArrayMap::iterator it = distributed_array_map_.find(
				label);
		check(it != distributed_array_map_.end(),
				"distributed/served array to restore with label " + label
						+ " not found");
		worker->set_per_array_map(array_id, it->second);
		distributed_array_map_.erase(it);
	}

	template<typename BLOCK_TYPE>
	friend std::ostream& operator<<(std::ostream&,
			const PersistentArrayManager<BLOCK_TYPE>&);

private:
	/** holder for saved contiguous arrays*/
	LabelContiguousArrayMap contiguous_array_map_;
	/** holder for saved distributed arrays*/
	LabelDistributedArrayMap distributed_array_map_;
	/** holder for saved scalar values */
	LabelScalarValueMap scalar_value_map_;
	/** holder for arrays and scalars that have been marked as persistent */
	ArrayIdLabelMap persistent_array_map_;

	/** MPI attribute */
	SIPMPIAttr& sip_mpi_attr_;

	/** inserts label value pair into map of saved values.
	 * warns if label has already been used. */

	void save_scalar(const std::string label, double value) {
		const std::pair<LabelScalarValueMap::iterator, bool> ret =
				scalar_value_map_.insert(
						std::pair<std::string, double>(label, value));
	if (!check_and_warn(ret.second, "Persistent array manager overwriting saved scalar with label " + label )) {
		scalar_value_map_.erase(ret.first);
		scalar_value_map_.insert(std::pair<std::string,double>(label,value));
	}
}

/** inserts label, Block pair into map of saved contiguous arrays.
 * Warns if label has already been used.
 * Fatal error if block is null
 */
void save_contiguous(const std::string label, Block* contig) {
	check(contig != NULL,
			"attempting to save nonexistent contiguous array");
	const std::pair<LabelContiguousArrayMap::iterator, bool> ret =
			contiguous_array_map_.insert(
					std::pair<std::string, Block*>(label, contig));
if (!check_and_warn(ret.second, "Overwriting label " + label + "with contiguous array")) {
			contiguous_array_map_.erase(ret.first);
			contiguous_array_map_.insert(std::pair<std::string, Block*>(label, contig));
		}

	}
	/** inserts label, map pair into map of saved contiguous arrays.
	 * Warns if label has already been used.
	 * Fatal error if block is null
	 */
	void save_distributed(const std::string label,
			typename IdBlockMap<BLOCK_TYPE>::PerArrayMap* map) {
		const std::pair<typename LabelDistributedArrayMap::iterator, bool> ret =
				distributed_array_map_.insert(
						std::pair<std::string,
								typename IdBlockMap<BLOCK_TYPE>::PerArrayMap*>(label,
								map));
	if (!check_and_warn(ret.second,
					"Overwriting label " + label + "with distributed array")) {
				distributed_array_map_.erase(ret.first);
				distributed_array_map_.insert(std::pair<std::string, typename IdBlockMap<BLOCK_TYPE>::PerArrayMap*>(label,map));
	}
}

void send_set_persistent(int array_id, int string_slot);
void send_restore_persistent(int array_id, int string_slot);

DISALLOW_COPY_AND_ASSIGN(PersistentArrayManager);

}
;

} /* namespace sip */

//#include "persistent_array_manager.cpp"

#endif /* PERSISTENT_ARRAY_MANAGER_H_ */
