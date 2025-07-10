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


MT::ThreadPool::ThreadPool(size_t NUM_THREADS) : logger(logger_mutex) {
	paused.store(true);
    stopped.store(false);
    logger_flag.store(true);
	completed_task_count = 0;
    last_task_id = 0;
	threads.reserve(NUM_THREADS);
	for (size_t i : std::ranges::iota_view(static_cast<size_t>(0), NUM_THREADS)) {
		threads.emplace_back();
	}
	for (size_t i : std::ranges::iota_view(static_cast<size_t>(0), NUM_THREADS)) {
		threads[i]._thread = std::move(std::thread(&ThreadPool::run, this, std::ref(threads[i])));
		threads[i].is_working.store(false);
	}
}


MT::ThreadPool::~ThreadPool() {
	wait();
	stopped.store(true);
	tasks_access.notify_all();
	clear_completed();
	for (size_t i : std::ranges::iota_view(static_cast<size_t>(0), threads.size())) {
        if (threads[i]._thread.joinable()) {
		    try {
                threads[i]._thread.join();
            } catch (...) {
                std::cerr << "Error joining thread\n";
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
	return completed_task_count == last_task_id;
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

            task->one_thread_pre_method();

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
		std::cout << "Result [" << task_id << "]: still processing...\n";
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
