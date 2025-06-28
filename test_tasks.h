#include <iostream>
#include <random>
#include <vector>
#include <algorithm>
#include <ranges>
#include <iterator>
#include <filesystem>
#include "thread_pool.h"



// Тип задачи
enum class TaskType {
	ComputePrimes, SortRandom, WaitEcho, SortBigVec
};


TaskType parseType(const std::string& cmd) {
    if (cmd == "compute_primes") return TaskType::ComputePrimes;
    if (cmd == "sort_random") return TaskType::SortRandom;
    if (cmd == "wait_echo") return TaskType::WaitEcho;
    if (cmd == "sort_big_vec") return TaskType::SortBigVec;
    throw std::runtime_error("Unknown command");
}



// Генерируем и сортируем массив из n элементов
struct SortRandom : public MT::Task {
    std::vector<int16_t> arr;
    size_t n;

    SortRandom(size_t n_, const std::string& description_ = "") : 
        MT::Task("Created and sorted array of " + std::to_string(n_) +  " elements:\n"), n(n_) {}

    void one_thread_method() override {
        arr.reserve(n);
        std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<> dist(std::numeric_limits<int16_t>::min(), std::numeric_limits<int16_t>::max());
        for (uint32_t i : std::ranges::iota_view(static_cast<uint32_t>(0), n)) {
            arr.push_back(dist(rng));
        }
        std::ranges::sort(arr);
        return;
    }

    void show_result() override {
        std::cout << description;
        std::ranges::copy_n(arr.begin(), n, std::ostream_iterator<int16_t>(std::cout, " "));
        std::cout << '\n';
        return;
    }
};



// Ищем все простые числа, меньшие, чем n
struct ComputePrimes : public MT::Task {
    std::vector<uint64_t> arr;
    size_t n;

    ComputePrimes(size_t n_, const std::string& description_ = "") : 
        MT::Task("Created a list of prime numbers from 1 to " + std::to_string(n_) + ":\n"), n(n_) {}

    void one_thread_method() override {
        std::vector<bool> value_flags(n + 1, true);
        for (uint64_t i = 2; i * i <= n; ++i) {
            if (value_flags[i]) {
                for (uint64_t j = i*i; j <= n; j += i) {
                    value_flags[j] = false;
                }
            }
        }
        for (uint64_t i = 2; i <= n; ++i) {
            if (value_flags[i] == true) {
                arr.push_back(i);
            }
        }
        return;
    }

    void show_result() override {
        std::cout << description;
        std::ranges::copy_n(arr.begin(), arr.size(), std::ostream_iterator<uint64_t>(std::cout, " "));
        std::cout << '\n';
        return;
    }
};



// блокируем поток на заданное кол-во секунд
struct WaitEcho : public MT::Task {
    size_t seconds;

    WaitEcho (size_t seconds_, const std::string message_, const std::string& description_ = "") : 
        MT::Task("[Waited " + std::to_string(seconds_) + "s] " + " with message: " + '"' + message_ + '"' +  "\n"), seconds(seconds_) {}

    void one_thread_method() override {
        std::this_thread::sleep_for(std::chrono::seconds(seconds));
    }

    void show_result() override {
        std::cout << description;
        return;
    }
};




struct SortingChunk;

// Сотрировка элементов файла, при этом многопоточная
struct SortBigVec : public MT::Task {
    std::mutex temp_files_mutex;
    std::condition_variable chunks_cv;
    std::atomic<size_t> completed_chunks{0};

    std::string file_name = "int_vec.txt";
    std::filesystem::path dir_name;

    std::vector<std::string> temp_files;
    const size_t chunk_size = 5'000'000;
    size_t n;


    friend struct SortingChunk;


    SortBigVec(size_t n_ = 3'000'000u, const std::string& description_ = "") : 
                MT::Task("Created and sorted file of " + std::to_string(n_) +  " elements:\n"), n(n_) {
        std::ofstream file(file_name);
        std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<> dist(std::numeric_limits<int16_t>::min(), std::numeric_limits<int16_t>::max());
        for (size_t i : std::ranges::iota_view(0u, n)) {
            file << dist(rng) << ' ';
        }
        // std::cout << "Complete generating file\n";
        size_t start_val = 1;
        while (std::filesystem::exists(std::to_string(start_val) + "_tmp_files") && 
               std::filesystem::is_directory(std::to_string(start_val) + "_tmp_files")) {
            ++start_val;
        }
        dir_name = std::to_string(start_val) + "_tmp_files";

        std::filesystem::create_directory(dir_name);
        file.close();
    }


    std::vector<int16_t> read_chunk(size_t chunk_size, std::ifstream& file) {
        std::vector<int16_t> chunk;
        chunk.reserve(chunk_size);
        std::ranges::copy_n(std::istream_iterator<int16_t>(file), chunk_size, std::back_insert_iterator(chunk));
        return chunk;
    }



    void merge_sorted_chunks() {
        std::vector<std::ifstream> inputs;

        auto compare = [](const std::pair<int16_t, size_t>& a, const std::pair<int16_t, size_t>& b) {
            return a.first > b.first;
        };

        std::priority_queue<std::pair<int16_t, size_t>, std::vector<std::pair<int16_t, size_t>>, decltype(compare)> min_heap;

        for (size_t i : std::ranges::iota_view(0u, temp_files.size())) {
            inputs.emplace_back(temp_files[i]);
            int value = *std::istream_iterator<int16_t>(inputs.back());
            min_heap.emplace(value, i);
        }

        std::ofstream out("../result_" + std::to_string(this->task_id) + ".txt");
        while (!min_heap.empty()) {
            std::pair<int16_t, size_t> copy_top = min_heap.top();
            out << copy_top.first << ' ';

            min_heap.pop();

            if (!inputs[copy_top.second].eof()) {
                size_t index = copy_top.second;
                int16_t new_value;
                if (inputs[index] >> new_value) {
                    min_heap.emplace(new_value, index);
                }
            }
        }

        return;
    }


    void one_thread_method() override {
        std::ifstream file(file_name);
        size_t expected_chunks = n / chunk_size + !(n % chunk_size == 0);

        while (!file.eof()) {
            std::vector<int16_t> chunk(std::move(read_chunk(chunk_size, file)));
			std::shared_ptr test{std::make_shared<SortingChunk>(chunk, *this)};
            thread_pool->add_task(test);
        }

        {
            std::unique_lock<std::mutex> lock(temp_files_mutex);
            chunks_cv.wait(lock, [&]() { return completed_chunks == expected_chunks; });
        }

        merge_sorted_chunks();
        return;
    }

    void show_result() override {
        bool is_correct_result = true;

        std::ifstream file("../result_" + std::to_string(this->task_id) + ".txt");
        int16_t prev_elem = *std::istream_iterator<int16_t>(file);
        for (size_t i : std::ranges::iota_view(1u, static_cast<size_t>(n - 1))) {
            int16_t new_elem = *std::istream_iterator<int16_t>(file);
            is_correct_result &= prev_elem <= new_elem;
            prev_elem = new_elem;
        }

        std::cout << description;
        if (is_correct_result) {
            std::cout << "The file was sorted correctly\n";
        } else {
            std::cout << "An error occurred while sorting the file\n";
        }
        return;
    }


    ~SortBigVec() {
        std::filesystem::remove_all(dir_name);
    }
};

struct SortingChunk : public MT::Task {
    std::vector<int16_t> arr; 
    SortBigVec& parrent;

    SortingChunk(std::vector<int16_t>& arr_, SortBigVec& parrent_) : 
        Task("Auxiliary task for sorting the chunk\n"), arr(std::move(arr_)), parrent(parrent_) {}


    void one_thread_method() override {
        std::ranges::sort(arr);
        std::string name_of_tmp_file = "./" + parrent.dir_name.string() + '/' + std::to_string(this->task_id) + ".txt";
        std::ofstream tmp_file(name_of_tmp_file);
        std::ranges::copy_n(arr.begin(), arr.size(), std::ostream_iterator<int16_t>(tmp_file, " "));
        {
            std::lock_guard<std::mutex> fm(parrent.temp_files_mutex);
            parrent.temp_files.push_back(name_of_tmp_file);
            parrent.completed_chunks.fetch_add(1);
        }
        parrent.chunks_cv.notify_one();
        return;
    }


    void show_result() override {
        std::cout << "The sorting of the chunk is completed\n";
        return;
    } 
};



