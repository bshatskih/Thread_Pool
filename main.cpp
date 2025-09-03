#include <iostream>
#include <random>
#include <vector>
#include <algorithm>
#include <ranges>
#include "thread_pool.h"
#include "test/test_tasks.h"


TaskType parseType(const std::string& cmd) {
    if (cmd == "compute_primes") return TaskType::ComputePrimes;
    if (cmd == "sort_random") return TaskType::SortRandom;
    if (cmd == "wait_echo") return TaskType::WaitEcho;
    if (cmd == "sort_big_vec") return TaskType::SortBigVec;
	if (cmd == "search_in_file") return TaskType::SearchInALargeFile;
    throw std::runtime_error("Unknown command");
}


int main() {
    MT::ThreadPool thread_pool(3);

    std::cout << "Server started. Enter commands:\n";
  	std::cout << "compute_primes N\n"
          	     "sort_random N\n"
          	     "wait_echo SECONDS MESSAGE\n"
                 "result ID\n"
				 "sort_big_vec\n"
				 "search_in_file\n"
				 "pause - to pause working server\n"
				 "start - to resume working server\n"
				 "count working threads - press '?'\n"
                 "exit\n";
	
	std::string input_data;
	while (std::getline(std::cin, input_data)) {
		std::stringstream ss(input_data);
		std::string command;
		ss >> command;

		if (command == "exit") {
			break;
		} else if (command == "result") {
			uint32_t cur_id = *std::istream_iterator<uint32_t>(ss);
			thread_pool.get_result(cur_id);
		} else if (command == "?") {
			std::cout << thread_pool.count_working_threads() << '\n';
		} else if (command == "pause") {
			thread_pool.pause();
		} else if (command == "start") {
			thread_pool.start();
		} else if (command == "!") {
			std::cout << thread_pool.count_waiting_threads() << '\n';
		}else {
			try {
				TaskType type = parseType(command);
				std::string data;
				std::getline(ss, data);
				if (type == TaskType::ComputePrimes) {
					std::shared_ptr test{std::make_shared<ComputePrimes>(std::stoi(data))};
					thread_pool.add_task(test);
				} else if (type == TaskType::SortRandom) {
					std::shared_ptr test{std::make_shared<SortRandom>(std::stoi(data))};
					thread_pool.add_task(test);
				} else if (type == TaskType::WaitEcho) {
					std::istringstream iss(data);
					uint32_t sec; 
					std::string message;
					iss >> sec;
					std::getline(iss, message);
					message.erase(message.begin());
					std::shared_ptr test{std::make_shared<WaitEcho>(sec, message)};
					thread_pool.add_task(test);
				} else if (type == TaskType::SortBigVec) {
					std::shared_ptr test{std::make_shared<SortBigVec>(std::stoi(data))};
					thread_pool.add_task(std::move(test));
				} if (type == TaskType::SearchInALargeFile) {
					std::string path_to_file, phrase;
					std::cin >> path_to_file >> phrase;
					std::cin.get();
					std::shared_ptr test{std::make_shared<SearchInALargeFile>(path_to_file, phrase)};
					thread_pool.add_task(std::move(test));
				}
		    } catch (std::exception& e) {
				std::cout << "Error: " << e.what() << '\n';
			}
		}
	}
	std::cout << "Server shut down.\n";
    return 0;
}