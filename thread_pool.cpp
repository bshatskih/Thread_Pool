#include "thread_pool.h"


MT::Task::Task(const std::string& description_) {
    description = description_;
    status = MT::Task::TaskStatus::awating;
    thread_pool = nullptr;
}


void MT::Task::one_thread_pre_method() {
	one_thread_method();
	status = MT::Task::TaskStatus::completed;
	return;
}


MT::ThreadPool::ThreadPool(size_t NUM_THREADS) : logger(logger_mutex), controller(*this) {
	paused.store(true);
    stopped.store(false);
    logger_flag.store(true);
	completed_task_count = 0;
    last_task_id = 0;
	threads.reserve(max_threads);

	for (size_t i : std::ranges::iota_view(static_cast<size_t>(0), NUM_THREADS)) {
		try {
			threads.emplace_back();
		} catch (const std::system_error& e) {
			std::string error_code_str = std::to_string(e.code().value()); 
			std::string error = std::string("When creating thread caught system_error with code ") + "[" + error_code_str + "] meaning " + "[" + e.what() + "]";
			std::time_t error_time = std::time(nullptr);
			
			{
				std::lock_guard<std::mutex> cm(cout_mutex);
				std::cerr << error << '\n';
			}
			std::lock_guard<std::mutex> lm(logger_mutex);
			logger.log_error(error_time, error);
			actual_threads_count = i;
			throw;
		}
	}
	
	for (size_t i : std::ranges::iota_view(static_cast<size_t>(0), NUM_THREADS)) {
		threads[i]._thread = std::move(std::thread(&ThreadPool::run, this, std::ref(threads[i])));
		threads[i].is_working.store(false);
		threads[i].is_waiting.store(false);
	}
	actual_threads_count = NUM_THREADS;
}


MT::ThreadPool::~ThreadPool() {
	wait();
	stopped.store(true);
	tasks_access.notify_all();
	clear_completed();
	for (size_t i : std::ranges::iota_view(static_cast<size_t>(0), actual_threads_count)) {
        if (threads[i]._thread.joinable()) {
		    try {
                threads[i]._thread.join();
            } catch (const std::system_error& e) {
				std::string error_code_str = std::to_string(e.code().value()); 
				std::string error = std::string("When joining thread caught system_error with code ") + "[" + error_code_str + "] meaning " + "[" + e.what() + "]";
				std::time_t error_time = std::time(nullptr);
				{
					std::lock_guard<std::mutex> cm(cout_mutex);
					std::cerr << error << '\n';
				}
				if (logger_flag.load()) {
					std::lock_guard<std::mutex> lm(logger_mutex);
					logger.log_error(error_time, error);
				}
            }
        }
	}
}


void MT::ThreadPool::pause() {
	if (paused.load() == false) {
    	paused.store(true);

		// логируем 
		if (logger_flag) {
			logger.log_paused(std::time(nullptr));
		}
	}
    return;
}


void MT::ThreadPool::start() {
    if (paused.load() == true) {
        paused.store(false);
        // даем всем потокам разрешающий сигнал для доступа к очереди невыполненных задач
        tasks_access.notify_all();
		// логируем
		if (logger_flag) {
			logger.log_start(std::time(nullptr));
		}
    }
    return;
}


bool MT::ThreadPool::is_comleted() const {
	return completed_task_count + incomplete_tasks_with_an_error.size() == last_task_id;
}


void MT::ThreadPool::set_logger_flag(bool flag) {
	logger_flag = flag;
}


void MT::ThreadPool::clear_completed() {
	std::lock_guard<std::mutex> lock(completed_tasks_mutex);
	completed_tasks.clear();
	return;
}


bool MT::ThreadPool::run_allowed() const {
	return (!task_queue.empty() && !paused.load());
}


void MT::ThreadPool::run(MT::Thread& _thread) {
   while (!stopped.load()) {
        std::unique_lock<std::mutex> lock(task_queue_mutex);

        tasks_access.wait(lock, [this]() -> bool { return run_allowed() || stopped.load(); });


        if (run_allowed()) {
            std::shared_ptr<Task> task = std::move(task_queue.front());
            _thread.is_working.store(true);
            task_queue.pop();
			lock.unlock();

			std::time_t start_time = std::time(nullptr);

			try {
            	task->one_thread_pre_method();
			} catch (const std::exception& e) {
				std::time_t error_time = std::time(nullptr);
				std::string error = std::string("Error when solving a problem with an id: ") + std::to_string(task->task_id) + ".\nException: " + e.what();
				std::lock_guard<std::mutex> cm(cout_mutex);
				std::cerr << error << '\n';

				if (logger_flag.load()) {
					std::lock_guard<std::mutex> lm(logger_mutex);
					logger.log_error(error_time, error);
				}
				std::lock_guard<std::mutex> itm(incomplete_tasks_with_an_error_mutex);
				incomplete_tasks_with_an_error.insert(task->task_id);
				continue;
			} catch (...) {
				std::time_t error_time = std::time(nullptr);
				std::lock_guard<std::mutex> cm(cout_mutex);
				std::string error = std::string("Unknown error in task id: ") + std::to_string(task->task_id);
				std::cerr << error << '\n';

				if (logger_flag.load()) {
					std::lock_guard<std::mutex> lm(logger_mutex);
					logger.log_error(error_time, error);
				}
				std::lock_guard<std::mutex> itm(incomplete_tasks_with_an_error_mutex);
				incomplete_tasks_with_an_error.insert(task->task_id);
				continue;
			}

			std::time_t end_time = std::time(nullptr);

            if (logger_flag.load()) {
				std::lock_guard<std::mutex> lg(logger_mutex);
				logger.add_record_about_task(start_time, end_time, task->description);
			}

            std::lock_guard<std::mutex> lg(completed_tasks_mutex);
			completed_tasks[task->task_id] = std::move(task);
			++completed_task_count;
			_thread.is_working.store(false);
        }
		wait_access.notify_one();
    }
}


void MT::ThreadPool::wait() {
	std::lock_guard<std::mutex> lock_wait(wait_mutex);

	start();

	std::unique_lock<std::mutex> lock(task_queue_mutex);
	wait_access.wait(lock, [this]()->bool { return is_comleted(); });

	pause();
}


void MT::ThreadPool::get_result(size_t task_id) {
	std::lock_guard<std::mutex> lock(completed_tasks_mutex);
	std::lock_guard<std::mutex> cl(cout_mutex);
	auto it = completed_tasks.find(task_id);
	if (it != completed_tasks.end()) {
		std::cout << "Result [" << task_id << "]:\n";
		it->second->show_result();
	} else if (task_id > last_task_id || task_id <= 0) {
		std::cout << "Unknown task ID\n";
	} else {
		std::lock_guard<std::mutex> itm(incomplete_tasks_with_an_error_mutex);
		if (incomplete_tasks_with_an_error.contains(task_id)) {
			std::cout << "An error occurred while completing the task\n";
		} else {
			std::cout << "Result [" << task_id << "]: still processing...\n";
		}
	}
	return;
}


size_t MT::ThreadPool::count_working_threads() {
	size_t result = 0;
	for (uint16_t i : std::ranges::iota_view(0u, threads.size())) {
		result += threads[i].is_working.load();
	}
	return result;
}


size_t MT::ThreadPool::count_of_threads() {
	return threads.size();
}


size_t MT::ThreadPool::count_waiting_threads() {
	size_t result = 0;
	for (uint16_t i : std::ranges::iota_view(0u, threads.size())) {
		result += threads[i].is_waiting.load();
	}
	return result;
}


MT::ThreadPoolController::ThreadPoolController(MT::ThreadPool& pool_ref) : pool(pool_ref) {
	controller_thread = std::move(std::thread(&ThreadPoolController::monitor, this));
	
}


MT::ThreadPoolController::~ThreadPoolController() {
	stopped.store(true);
	if (controller_thread.joinable()) {
		controller_thread.join();
	}
}


void MT::ThreadPoolController::monitor() {
	while(!stopped.load()) {
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
        check_for_deadlock();
	}
}


void MT::ThreadPoolController::check_for_deadlock() {
	size_t count_of_threads = pool.count_of_threads();
	size_t count_of_waiting_threads = pool.count_waiting_threads();

	if (count_of_threads == count_of_waiting_threads) {
		std::time_t deadlock_time = std::time(nullptr);
		{
			std::lock_guard<std::mutex> cm(pool.cout_mutex);
			std::cerr << "Deadlock detected! Expanding pool...\n";
		}

		if (pool.logger_flag.load()) {
			std::lock_guard<std::mutex> lm(pool.logger_mutex);
			pool.logger.log_deadlock(deadlock_time, pool.count_of_threads());
		}
		pool.expand();
	}
}


void MT::ThreadPool::expand() {
	if (count_of_threads() == max_threads) {
		std::string error_str = "The number of open threads has exceeded the maximum allowed limit.";
		std::time_t error_time = std::time(nullptr);

		{
			std::lock_guard<std::mutex> cm(cout_mutex);
			std::cerr << error_str << "\nFurther pool operation is not possible\n";
		}

		if (logger_flag.load()) {
			std::lock_guard<std::mutex> lm(logger_mutex);
			logger.log_error(error_time, error_str);
		}
		throw;
	}
    std::lock_guard<std::mutex> lock(task_queue_mutex);
    try {
		threads.emplace_back();
		threads.back()._thread = std::move(std::thread(&ThreadPool::run, this, std::ref(threads.back())));
		threads.back().is_working.store(false);
		threads.back().is_waiting.store(false);
		actual_threads_count++;
	} catch (const std::system_error& e) {
		std::string error_code_str = std::to_string(e.code().value()); 
		std::string error = std::string("When creating thread caught system_error with code ") + "[" + error_code_str + "] meaning " + "[" + e.what() + "]";
		std::time_t error_time = std::time(nullptr);
		
		{
			std::lock_guard<std::mutex> cm(cout_mutex);
			std::cerr << error << '\n';
		}
		std::lock_guard<std::mutex> lm(logger_mutex);
		logger.log_error(error_time, error);
		--actual_threads_count;
		throw;
	}
}


void MT::ThreadPool::set_current_thread_waiting(bool waiting_status) {
	std::thread::id current_id = std::this_thread::get_id();
	for (auto& thread : threads) {
		if (thread._thread.get_id() == current_id) {
			thread.is_waiting.store(waiting_status);
			return;
		}
	}
}


