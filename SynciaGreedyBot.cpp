#ifdef SYNCIAGREEDYBOT_UNIT_TEST
#include "SynciaGreedyBot.h"
#include <iostream>

using namespace syncia;

int main(int argc, char* argv[])
{
	boost::asio::io_service service;
	boost::asio::io_service::work w(service);
	boost::thread_group thread_group;
	int thread_num = 4;
	for(int i = 0; i < thread_num; ++i){
		thread_group.create_thread(boost::bind(&boost::asio::io_service::run, &service));
	}
	int local_port = 54321;
	auto hostname = std::string("localhost");
	if(argc > 1){
		hostname = std::string(argv[1]);
		if(argc == 3)
		{
			local_port = boost::lexical_cast<int>(std::string(argv[2]));
		}	
	}
	auto node_id = neuria::network::CreateSocketNodeId(hostname, local_port);
	std::cout << "NodeId is " << node_id << std::endl; 

	const int buffer_size = 128;
	const unsigned int max_key_hash_count = 30;
	const unsigned int spread_key_hash_max_count = 200;
	const unsigned int max_hop_count = 6;

	std::stringstream no_output;
	std::ostream& os = std::cout;

	auto server = neuria::network::SocketServer::Create(
		service, local_port, buffer_size, os);
	auto dispatcher = BehaviorDispatcher::Create(service, os);

	auto client = neuria::network::SocketClient::Create(
		service, buffer_size, os);
	
	auto upper_session_pool = neuria::network::SessionPool::Create();
	auto lower_session_pool = neuria::network::SessionPool::Create();
	auto file_db = database::FileKeyHashDb::Create(0.3, buffer_size, std::cout);
	auto searched_file_db = 
		database::FileKeyHashDb::Create(0.3, buffer_size, std::cout);

	auto syncia = SynciaGreedyBot::Create(
		max_key_hash_count, spread_key_hash_max_count, max_hop_count, buffer_size, 
		upper_session_pool, lower_session_pool, file_db, searched_file_db, 
		node_id, std::cout);
	syncia->Bind(client);
	syncia->Bind(dispatcher);
	auto multiple_timer = neuria::timer::MultipleTimer::Create(service);
	syncia->Bind(multiple_timer);
	syncia->Initialize();

	auto shell = neuria::test::CuiShell(std::cout);
	neuria::test::RegisterExitFunc(shell);
	
	shell.Register("upload", "add upload directory.", 
		[syncia](const neuria::test::CuiShell::ArgList& args){
			syncia->AddUploadDirectory(FileSystemPath(args.at(1)));
		});
	shell.Register("db", "show uploaded files.", 
		[file_db](const neuria::test::CuiShell::ArgList& args){
			std::cout << file_db << std::endl;
		});
	shell.Register("sdb", "show uploaded files.", 
		[searched_file_db](const neuria::test::CuiShell::ArgList& args){
			std::cout << searched_file_db << std::endl;
		});
	
	
	shell.Register("request", "hostname port id downloadpath: request file.", 
		[syncia, file_db](const neuria::test::CuiShell::ArgList& args){
			const auto file_key_hash = 
				file_db->Get(boost::lexical_cast<unsigned int>(args.at(1)));
			syncia->RequestFile(file_key_hash);
		});
	
	shell.Register("upper", "show upper linked sessions.", 
		[upper_session_pool](const neuria::test::CuiShell::ArgList& args){
			std::cout << "upper session: " << upper_session_pool << std::endl;
		});
	shell.Register("lower", "show lower linked sessions.", 
		[lower_session_pool](const neuria::test::CuiShell::ArgList& args){
			std::cout << "lower session: " << lower_session_pool << std::endl;
		});
	
	server->StartAccept(
		neuria::network::Server::OnAcceptedFunc([dispatcher](
				neuria::network::Session::Pointer session){
			session->StartReceive(dispatcher->GetOnReceivedFunc());
		}),
		neuria::network::Server::OnFailedAcceptFunc([](
				const neuria::network::ErrorCode){
			//nothing
		}),
		neuria::network::Session::OnClosedFunc([](
				neuria::network::Session::Pointer){
			//nothing
		})
	);
	shell.Start();
	thread_group.join_all();

    return 0;
}

#endif
