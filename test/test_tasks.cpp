#include "test_tasks.h"
#include <exception>

void SortRandom::one_thread_method() {
    arr.reserve(n);
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<> dist(std::numeric_limits<int16_t>::min(), std::numeric_limits<int16_t>::max());
    for (uint32_t i : std::ranges::iota_view(static_cast<uint32_t>(0), n)) {
        arr.push_back(dist(rng));
    }
    std::ranges::sort(arr);
    return;
}


void SortRandom::show_result() {
    std::cout << description;
    std::ranges::copy_n(arr.begin(), n, std::ostream_iterator<int16_t>(std::cout, " "));
    std::cout << '\n';
    return;
}


SortRandom::SortRandom(size_t n_) : 
        MT::Task("Created and sorted array of " + std::to_string(n_) +  " elements:\n"), n(n_) {}




void ComputePrimes::one_thread_method() {
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


void ComputePrimes::show_result() {
    std::cout << description;
    std::ranges::copy_n(arr.begin(), arr.size(), std::ostream_iterator<uint64_t>(std::cout, " "));
    std::cout << '\n';
    return;
}


ComputePrimes::ComputePrimes(size_t n_) : 
    MT::Task("Created a list of prime numbers from 1 to " + std::to_string(n_) + ":\n"), n(n_) {}




void WaitEcho::one_thread_method() {
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
}


void WaitEcho::show_result() {
    std::cout << description;
    return;
}


WaitEcho::WaitEcho(size_t seconds_, const std::string message_) : 
        MT::Task("[Waited " + std::to_string(seconds_) + "s] " + " with message: " + '"' + message_ + '"' +  "\n"), seconds(seconds_) {}




SortBigVec::SortBigVec(size_t n_) : 
                MT::Task("Created and sorted file of " + std::to_string(n_) +  " elements:\n"), n(n_) {
    file_id = 1;
    while (std::filesystem::exists(std::to_string(file_id) + "_int_vec.txt")) {
        ++file_id;
    }
    file_name = std::to_string(file_id) + "_int_vec.txt";
    std::ofstream file(file_name);
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<> dist(std::numeric_limits<int16_t>::min(), std::numeric_limits<int16_t>::max());
    for (size_t i : std::ranges::iota_view(0u, n)) {
        file << dist(rng) << ' ';
    }

    // {
    //     std::lock_guard<std::mutex> lc(thread_pool->cout_mutex);
    //     std::cout << "Complete generating file\n";
    // }

    dir_name = std::to_string(file_id) + "_tmp_files";

    std::filesystem::create_directory(dir_name);
    file.close();
}


std::vector<int16_t> SortBigVec::read_chunk(size_t chunk_size, std::ifstream& file) {
    std::vector<int16_t> chunk;
    chunk.reserve(chunk_size);
    std::ranges::copy_n(std::istream_iterator<int16_t>(file), chunk_size, std::back_insert_iterator(chunk));
    return chunk;
}


void SortBigVec::merge_sorted_chunks() {
    std::vector<std::ifstream> inputs;

    auto compare = [](const std::pair<int16_t, size_t>& a, const std::pair<int16_t, size_t>& b) {
        return a.first > b.first;
    };

    std::priority_queue<std::pair<int16_t, size_t>, std::vector<std::pair<int16_t, size_t>>, decltype(compare)> min_heap;

    for (size_t i : std::ranges::iota_view(0u, temp_files.size())) {
        inputs.emplace_back(temp_files[i]);
        int16_t value = *std::istream_iterator<int16_t>(inputs.back());
        min_heap.emplace(value, i);
    }

    std::ofstream out("../result_" + std::to_string(file_id) + ".txt");
    while (!min_heap.empty()) {
        std::pair<int16_t, size_t> copy_top = min_heap.top();
        out << copy_top.first << ' ';

        min_heap.pop();
        size_t index = copy_top.second;
        int16_t new_value;
        if (inputs[index] >> new_value) {
            min_heap.emplace(new_value, index);
        } else {
            inputs[index].close();
        }
    }

    for (size_t i : std::ranges::iota_view(0u, temp_files.size())) {
        if (inputs[i].is_open()) {
            inputs[i].close();
        }
    }
    out.close();

    return;
}


void SortBigVec::one_thread_method() {
    std::ifstream file(file_name);
    size_t expected_chunks = n / chunk_size + !(n % chunk_size == 0);

    while (!file.eof()) {
        std::vector<int16_t> chunk(std::move(read_chunk(chunk_size, file)));
        std::shared_ptr test{std::make_shared<SortingChunk>(chunk, *this)};
        thread_pool->add_task(std::move(test));
    }

    std::unique_lock<std::mutex> lock(temp_files_mutex);
    chunks_cv.wait(lock, [&]() { return completed_chunks.load() == expected_chunks; });

    merge_sorted_chunks();
    file.close();
    return;
}


void SortBigVec::show_result() {
    bool is_correct_result = true;

    std::ifstream file("../result_" + std::to_string(file_id) + ".txt");
    int16_t prev_elem = *std::istream_iterator<int16_t>(file);
    int16_t new_elem;
    size_t count = 1;
    while (file >> new_elem) {
        is_correct_result &= prev_elem <= new_elem;
        prev_elem = new_elem;
        ++count;
    }

    std::cout << description;
    if (is_correct_result && count == n) {
        std::cout << "The file was sorted correctly\n";
    } else {
        std::cout << "An error occurred while sorting the file\n";
    }
    file.close();
    return;
}


SortBigVec::~SortBigVec() {
    std::unique_lock<std::mutex> lock(temp_files_mutex);
    chunks_cv.wait(lock, [&]() { return completed_chunks.load() == n / chunk_size + !(n % chunk_size == 0); });

    for (size_t i : std::ranges::iota_view(0u, temp_files.size())) {
        std::filesystem::remove("./" + dir_name.string() + '/' + temp_files[i]);
    }
    std::filesystem::remove_all(dir_name);
    std::filesystem::remove(file_name);
    std::filesystem::remove("../result_" + std::to_string(file_id) + ".txt");
}




SortingChunk::SortingChunk(std::vector<int16_t>& arr_, SortBigVec& parrent_) : 
    Task("Auxiliary task for sorting the chunk\n"), arr(std::move(arr_)), parrent(parrent_) {}


void SortingChunk::one_thread_method() {
    std::ranges::sort(arr);
    std::string name_of_tmp_file = "./" + parrent.dir_name.string() + '/' + std::to_string(this->task_id) + ".txt";
    std::ofstream tmp_file(name_of_tmp_file);
    std::ranges::copy_n(arr.begin(), arr.size(), std::ostream_iterator<int16_t>(tmp_file, " "));
    {
        std::lock_guard<std::mutex> fm(parrent.temp_files_mutex);
        parrent.temp_files.push_back(name_of_tmp_file);
        parrent.completed_chunks.fetch_add(1);
        parrent.chunks_cv.notify_one();
    }
    tmp_file.close();
    arr.clear();
    arr.shrink_to_fit();
    return;
}


void SortingChunk::show_result() {
    std::cout << "The sorting of the chunk is completed\n";
    return;
} 




SearchInALargeFile::SearchInALargeFile(const std::string& path_to_file_, const std::string& phrase_) :
    MT::Task(std::string("Search for the word - ") + '"' + phrase_ + '"' + ", in a file: " + path_to_file_ + '\n'), 
    path_to_file(path_to_file_), word(phrase_) {
    std::ifstream file(path_to_file);
    if (!file.is_open()) {
        std::cout << "Couldn't open the file at the specified address\n";
    }
    file.close();
}


void SearchInALargeFile::one_thread_method() {
    std::ifstream file(path_to_file);

    std::vector<std::pair<size_t, std::string>> chunc;
    std::string cur_str;
    size_t cur_str_number = 1;
    size_t expected_chunks = 0;
    while (std::getline(file, cur_str)) {
        chunc.emplace_back(cur_str_number, cur_str);
        if (chunc.size() == chunk_size) {
            std::shared_ptr test = std::make_shared<SearchInAChunk>(*this, std::move(chunc));
            thread_pool->add_task(test);
            chunc.clear();
            ++expected_chunks;
        }
        ++cur_str_number;
    }

    std::unique_lock<std::mutex> ifm(information_found_mutex);
    information_cv.wait(ifm, [&]() { return completed_chunks.load() == expected_chunks; });

    file.close();

    return;
}


void SearchInALargeFile::show_result() {
    std::cout << description;
    std::lock_guard<std::mutex> ifm(information_found_mutex);

    size_t count = 0;
    for (auto it = information_found.cbegin(); it != information_found.cend(); ++it) {
        count += it->second.second;
    }
    std::cout << std::to_string(count) + " occurrences of the word " + '"' + word + '"' + " were found in the text\n";
    std::cout << "if you want to see the lines in which the word occurs, press 'Y' or 'N' otherwise\n";
    char command = *std::istream_iterator<char>(std::cin);
    if (command == 'Y') {
        for (auto it = information_found.cbegin(); it != information_found.cend(); ++it) {
            std::cout << "Line: " << std::to_string(it->first) + ", quantity: " + std::to_string(it->second.second) + ", " + '"' + it->second.first + '"' + '\n';
        }
    }
    std::cin.get();
    return;
}


SearchInAChunk::SearchInAChunk(SearchInALargeFile& parrent_, std::vector<std::pair<size_t, std::string>>&& chunc_) :
    MT::Task("Auxiliary task for searching in a file\n"), parrent(parrent_), chunc(std::move(chunc_)) {}



std::vector<size_t> ComputeLPS(const std::string& word) {
    size_t m = word.length();
    std::vector<size_t> lps(m, 0);
    size_t len = 0; 

    for (size_t i = 1; i < m; ) {
        if (word[i] == word[len]) {
            len++;
            lps[i] = len;
            i++;
        } else {
            if (len != 0) {
                len = lps[len - 1];
            } else {
                lps[i] = 0;
                i++;
            }
        }
    }
    return lps;
}


void SearchInAChunk::one_thread_method() {
    size_t m = parrent.word.length();
    for (size_t k : std::ranges::iota_view(0u, chunc.size())) {
        const std::string& text = chunc[k].second;
        const std::string& word = parrent.word;
        size_t n = text.size();
        if (m == 0 || n < m) {
            continue;
        }

        std::vector<size_t> lps = ComputeLPS(word);
        size_t i = 0, j = 0, cur_count = 0;

        while (i < n) {
            if (text[i] == word[j]) {
                i++;
                j++;
                if (j == m) { 
                    ++cur_count;
                    j = lps[j - 1];
                }
            } else {
                if (j != 0) {
                    j = lps[j - 1];
                } else {
                    i++;
                }
            }
        }

        if (cur_count != 0) {
            std::lock_guard<std::mutex> ifm(parrent.information_found_mutex);
            parrent.information_found[chunc[k].first] = std::make_pair(text, cur_count);
        }
    }
    parrent.completed_chunks.fetch_add(1);
    parrent.information_cv.notify_one();
    return;
}


void SearchInAChunk::show_result() {
    std::cout << "Auxiliary task for searching in a file is completed\n";
    return;
}

