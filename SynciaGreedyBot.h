#pragma once
//SynciaGreedyBot:20120926
#include <iostream>
#include "syncia/Syncia.h"

namespace syncia
{

class SynciaGreedyBot : public boost::enable_shared_from_this<SynciaGreedyBot>{
public:
	using Pointer = boost::shared_ptr<SynciaGreedyBot>;

	static auto Create(
			unsigned int max_key_hash_count, 
			unsigned int spread_key_hash_max_count, 
			unsigned int max_hop_count, 
			unsigned int buffer_size,
			neuria::network::SessionPool::Pointer upper_session_pool,
			neuria::network::SessionPool::Pointer lower_session_pool,
			database::FileKeyHashDb::Pointer file_db, 
			database::FileKeyHashDb::Pointer searched_file_db, 
			const neuria::network::NodeId& node_id, 
			std::ostream& os) -> Pointer {
		auto upload_action = UploadAction::Create(file_db, node_id, os);
		
		auto fetch_link_behavior = LinkBehavior::Create(lower_session_pool, 
			command::GetCommandId<command::LinkForFetchKeyHashCommand>(), os);

		auto search_key_hash_behavior = SearchKeyHashBehavior::Create(
			node_id, max_key_hash_count, max_hop_count, 
			upper_session_pool, file_db, searched_file_db, os);
	
		auto spread_key_hash_action = 
			SpreadKeyHashAction::Create(lower_session_pool, node_id, os);
		auto spread_key_hash_behavior = SpreadKeyHashBehavior::Create(
			node_id, spread_key_hash_max_count, max_hop_count, 
			lower_session_pool, file_db, os);

		auto request_file_action = 
			RequestFileAction::Create(FileSystemPath("./updownload"), os);
		auto request_file_behavior = RequestFileBehavior::Create(
			buffer_size, file_db, os);

		return Pointer(new SynciaGreedyBot(upload_action, 
			fetch_link_behavior, 
			search_key_hash_behavior,
			spread_key_hash_action, spread_key_hash_behavior,
			request_file_action, request_file_behavior,
			file_db, node_id, 
			os));
	}

	auto Initialize() -> void {
		this->AddUploadDirectory(syncia::FileSystemPath("./updownload"));
	}

	auto SetOnReceivedSearchKeyHashAnswerFunc(
			SearchKeyHashBehavior::OnReceivedAnswerFunc on_received_func) -> void {
		this->search_key_hash_behavior->SetOnReceivedAnswerFunc(on_received_func);
	}

	auto SetOnRepliedFileFunc(
			RequestFileAction::OnRepliedFileFunc on_replied_file_func) -> void {
		this->on_replied_file_func = on_replied_file_func;
	}

	auto SetDownloadDirectoryPath(const FileSystemPath& path) -> void {
		this->request_file_action->SetDownloadDirectoryPath(path);
	}

	auto AddUploadDirectory(const FileSystemPath& upload_directory_path) -> void {
		auto watcher = 
			filesystem::AddRemoveFileInDirectoryWatcher::Create(upload_directory_path);
		filesystem::SetOnAddedFileFunc(watcher, 
			[this](const FileSystemPath& file_path){
				this->shared_from_this()->upload_action->UploadFile(file_path);
			}
		);
		filesystem::SetOnRemovedFileFunc(watcher,
			[this](const FileSystemPath& file_path){
				this->shared_from_this()->file_db->Erase(file_path);
			}
		);
		//this->add_remove_watcher_list.push_back(watcher);
		this->multiple_timer->AddCallbackFuncAndStartTimer(
			5,
			[watcher]() -> neuria::timer::IsContinue {
				watcher->Check();
				watcher->Call();
				watcher->Update();
				return neuria::timer::IsContinue(true);
			}
		);
	}

	auto RequestFile(const database::FileKeyHash& key_hash) -> void {
		this->request_file_action->RequestFile(key_hash, this->on_replied_file_func);
	}

	auto Bind(neuria::network::Client::Pointer client) -> void {
		this->fetch_link_behavior->Bind(client);
		this->search_key_hash_behavior->Bind(client);
		this->spread_key_hash_behavior->Bind(client);
		this->request_file_action->Bind(client);
	}
	
	auto Bind(BehaviorDispatcher::Pointer dispatcher) -> void {
		this->fetch_link_behavior->Bind(dispatcher);
		this->search_key_hash_behavior->Bind(dispatcher);
		this->spread_key_hash_behavior->Bind(dispatcher);
		this->request_file_behavior->Bind(dispatcher);
	}
	
	auto Bind(neuria::timer::MultipleTimer::Pointer multiple_timer) -> void {
		this->multiple_timer = multiple_timer;
		this->multiple_timer->AddCallbackFuncAndStartTimer(
			10, 
			[this]() -> neuria::timer::IsContinue {
				this->file_db->Apply(
					[this](database::FileKeyHash& key_hash){ 
						if(key_hash.GetOwnerId() == this->node_id){
							key_hash.SetBirthTimeNow();
						}
					}
				);
				return neuria::timer::IsContinue(true);
			}
		);
		this->multiple_timer->AddCallbackFuncAndStartTimer(
			10, 
			[this]() -> neuria::timer::IsContinue {
				this->spread_key_hash_action->RequestSpreadKeyHash();
				return neuria::timer::IsContinue(true);
			}
		);
		this->multiple_timer->AddCallbackFuncAndStartTimer(
			30, 
			[this]() -> neuria::timer::IsContinue {
				/*
				for(const auto key_hash : this->file_db->GetFileKeyHashList()){
					if(key_hash.GetOwnerId() != this->node_id){
						this->RequestFile(key_hash);
					}
				}*/

				for(const auto target_key_hash : this->file_db->GetFileKeyHashList()){
					bool is_already_exist = false;
					for(const auto key_hash : this->file_db->GetFileKeyHashList()){
						if(target_key_hash.GetHashId()() == key_hash.GetHashId()()
							&& key_hash.GetOwnerId() == this->node_id){
							is_already_exist = true;
						}
					}
					if(!is_already_exist){
						this->RequestFile(target_key_hash);
					}

				}
				return neuria::timer::IsContinue(true);
			}
		);

	}

private:
    SynciaGreedyBot(
		UploadAction::Pointer upload_action, 
		LinkBehavior::Pointer fetch_link_behavior,
		SearchKeyHashBehavior::Pointer search_key_hash_behavior, 
		SpreadKeyHashAction::Pointer spread_key_hash_action, 
		SpreadKeyHashBehavior::Pointer spread_key_hash_behavior, 
		RequestFileAction::Pointer request_file_action,
		RequestFileBehavior::Pointer request_file_behavior,
		database::FileKeyHashDb::Pointer file_db,
		const neuria::network::NodeId& node_id, 
		std::ostream& os) 
			:upload_action(upload_action), 
			fetch_link_behavior(fetch_link_behavior), 
			search_key_hash_behavior(search_key_hash_behavior),
			spread_key_hash_action(spread_key_hash_action),
			spread_key_hash_behavior(spread_key_hash_behavior),
			request_file_action(request_file_action),
			request_file_behavior(request_file_behavior),
			file_db(file_db), node_id(node_id), 
			os(os){

		
	}

	neuria::timer::MultipleTimer::Pointer multiple_timer;
	std::vector<filesystem::AddRemoveFileInDirectoryWatcher> add_remove_watcher_list;

	UploadAction::Pointer upload_action;
	
	LinkBehavior::Pointer fetch_link_behavior;

	SearchKeyHashAction::Pointer search_key_hash_action;
	SearchKeyHashBehavior::Pointer search_key_hash_behavior;

	SpreadKeyHashAction::Pointer spread_key_hash_action;
	SpreadKeyHashBehavior::Pointer spread_key_hash_behavior;

	RequestFileAction::Pointer request_file_action;
	RequestFileBehavior::Pointer request_file_behavior;

	database::FileKeyHashDb::Pointer file_db;
	neuria::network::NodeId node_id;

	RequestFileAction::OnRepliedFileFunc on_replied_file_func;
	std::ostream& os;
};

}

