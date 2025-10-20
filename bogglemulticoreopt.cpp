//
// boggle.cpp : Ultra-optimized Boggle board generator and solver
//

#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <chrono>
#include <ctime>
#include <random>
#include <algorithm>
#include <thread>
#include <vector>
#include <functional>

using namespace std;

// Flat trie implementation
int* flat_trie_data = nullptr;
int flat_trie_size = 0;
int flat_trie_next_free = 1;

thread_local long long thread_lookups = 0;
char letter_sample[26];
int letter_sample_size = 0;

// Optimized adjacency lookup
const int moves[16][8] = {
    {1, 4, 5, -1, -1, -1, -1, -1},
    {0, 2, 4, 5, 6, -1, -1, -1},
    {1, 3, 5, 6, 7, -1, -1, -1},
    {2, 6, 7, -1, -1, -1, -1, -1},
    {0, 1, 5, 8, 9, -1, -1, -1},
    {0, 1, 2, 4, 6, 8, 9, 10},
    {1, 2, 3, 5, 7, 9, 10, 11},
    {2, 3, 6, 10, 11, -1, -1, -1},
    {4, 5, 9, 12, 13, -1, -1, -1},
    {4, 5, 6, 8, 10, 12, 13, 14},
    {5, 6, 7, 9, 11, 13, 14, 15},
    {6, 7, 10, 14, 15, -1, -1, -1},
    {8, 9, 13, -1, -1, -1, -1, -1},
    {8, 9, 10, 12, 14, -1, -1, -1},
    {9, 10, 11, 13, 15, -1, -1, -1},
    {10, 11, 14, -1, -1, -1, -1, -1}
};

const int letter_scores[26] = { 1,2,3,2,1,4,2,4,1,8,5,1,3,1,1,3,10,1,1,1,1,4,4,8,4,10 };

int wordbonus[3][16] = {
    { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },
    { 2,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },
    { 3,2,2,1,1,1,1,1,1,1,1,1,1,1,1,1 }
};

int letterbonus[3][16] = {
    { 3,2,2,1,1,1,1,1,1,1,1,1,1,1,1,1 },
    { 3,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },
    { 3,3,2,2,1,1,1,1,1,1,1,1,1,1,1,1 }
};

thread_local int* thread_scores = nullptr;
thread_local int* thread_word_indices = nullptr;

// Xorshift+ Random Number Generator - FASTEST
thread_local uint64_t xorshift_state[2] = {123456789ULL, 987654321ULL};

inline void fast_srand(uint64_t seed) {
    xorshift_state[0] = seed;
    xorshift_state[1] = seed ^ 0x123456789ABCDEFULL;
}

inline uint64_t xorshift128plus(void) {
    uint64_t x = xorshift_state[0];
    uint64_t const y = xorshift_state[1];
    xorshift_state[0] = y;
    x ^= x << 23;
    xorshift_state[1] = x ^ y ^ (x >> 17) ^ (y >> 26);
    return xorshift_state[1] + y;
}

inline int fast_rand(void) {
    return xorshift128plus() & 0x7FFF;
}

// Vowel check - optimized
inline bool is_vowel(char c) {
    return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u';
}

struct State {
    char board[16];
    int score_map[16];
    char running_string[16];
    int wordcount;
    bool valid;
    bool quickpass;
    int* wordbonusmap;
    int* letterbonusmap;
    char* list_words[4096];
    int list_scores[4096];
    int* score_cleanup[4096];
    int* scores;

    State() : wordcount(0), valid(false), quickpass(false), wordbonusmap(nullptr), letterbonusmap(nullptr), scores(nullptr) {}
};

inline void init_flat_trie() {
    flat_trie_size = 27 * 200000;
    flat_trie_data = new int[flat_trie_size]();
}

inline int flat_trie_allocate_node() {
    if (flat_trie_next_free * 27 >= flat_trie_size) {
        int new_size = flat_trie_size * 2;
        int* new_data = new int[new_size]();
        memcpy(new_data, flat_trie_data, flat_trie_size * sizeof(int));
        delete[] flat_trie_data;
        flat_trie_data = new_data;
        flat_trie_size = new_size;
    }
    return flat_trie_next_free++;
}

inline void build_trie_flat() {
    init_flat_trie();
    int count = 0;
    ifstream newfile;
    newfile.open("words.txt", ios::in);
    if (newfile.is_open()) {
        string tp;
        while (getline(newfile, tp)) {
            if (!tp.empty() && tp.back() == '\r') tp.pop_back();
            if (tp.empty()) continue;

            int current = 0;
            for (char c : tp) {
                char lower_c = c | 0x20;
                if (lower_c < 'a' || lower_c > 'z') continue;

                int letter_index = lower_c - 'a';
                int node_offset = current * 27 + letter_index;

                if (flat_trie_data[node_offset] == 0) {
                    int new_node = flat_trie_allocate_node();
                    flat_trie_data[node_offset] = new_node;
                }
                current = flat_trie_data[node_offset];
            }
            flat_trie_data[current * 27 + 26] = 1;
            count++;
        }
        newfile.close();
    }
    cout << "Flat trie built with " << flat_trie_next_free << " nodes (" << count << " words)" << endl;
}

inline void initialise_probability() {
    ifstream newfile;
    newfile.open("letters.txt", ios::in);
    if (newfile.is_open()) {
        string tp;
        getline(newfile, tp);
        letter_sample_size = min((int)tp.length(), 26);
        for (int i = 0; i < letter_sample_size; i++) {
            letter_sample[i] = tp[i] | 0x20;
        }
        newfile.close();
    }
}

inline long search_letter_flat(char letter, int& current_node, int* scores) {
    thread_lookups++;

    int letter_index = letter - 'a';
    int node_offset = current_node * 27 + letter_index;

    if (flat_trie_data[node_offset] == 0) {
        return 0;
    }

    current_node = flat_trie_data[node_offset];

    if (flat_trie_data[current_node * 27 + 26] != 0) {
        return (long)&scores[current_node];
    }

    return 1;
}

inline void words_from_flat(State& state, int current_node, int position, int depth, int running_score, int running_multiplier) {
    char letter = state.board[position];
    state.running_string[depth] = letter;
    depth++;
    running_score = running_score + state.score_map[position] * state.letterbonusmap[position];
    running_multiplier = running_multiplier * state.wordbonusmap[position];

    long result = search_letter_flat(letter, current_node, state.scores);

    if (depth >= 2 && result > 0) {
        if (result > 1) {
            state.valid = true;
            if (state.quickpass) return;

            int finalscore = (running_score * running_multiplier) + depth * 2;
            int* score_location = (int*)result;

            if (*score_location == 0) {
                if (state.wordcount >= 4096) return;
                state.list_words[state.wordcount] = (char*)malloc(depth + 1);
                memcpy(state.list_words[state.wordcount], state.running_string, depth);
                state.list_words[state.wordcount][depth] = 0;

                state.list_scores[state.wordcount] = finalscore;
                *score_location = finalscore;
                state.score_cleanup[state.wordcount] = score_location;
                thread_word_indices[current_node] = state.wordcount;
                state.wordcount++;
            } else if (*score_location < finalscore) {
                *score_location = finalscore;
                int idx = thread_word_indices[current_node];
                state.list_scores[idx] = finalscore;
            }
        }
    } else if (result == 0) {
        return;
    }

    char temp = state.board[position];
    state.board[position] = '-';

    const int* move_ptr = moves[position];
    int i = 0;

    if (position == 5 || position == 6 || position == 9 || position == 10) {
        for (; i < 8; i++) {
            int move = move_ptr[i];
            if (move == -1) break;
            if (state.board[move] != '-') {
                words_from_flat(state, current_node, move, depth, running_score, running_multiplier);
                if (state.quickpass && state.valid) break;
            }
        }
    } else if (position == 0 || position == 3 || position == 12 || position == 15) {
        for (; i < 3; i++) {
            int move = move_ptr[i];
            if (move == -1) break;
            if (state.board[move] != '-') {
                words_from_flat(state, current_node, move, depth, running_score, running_multiplier);
                if (state.quickpass && state.valid) break;
            }
        }
    } else {
        for (; i < 5; i++) {
            int move = move_ptr[i];
            if (move == -1) break;
            if (state.board[move] != '-') {
                words_from_flat(state, current_node, move, depth, running_score, running_multiplier);
                if (state.quickpass && state.valid) break;
            }
        }
    }

    state.board[position] = temp;
}

inline void generate_flat(int round, char* out_board, int* out_letterbonusmap, int* out_wordbonusmap, int* out_wordcount, int* out_list_scores, char** out_list_words) {
    static thread_local int shuffle_count[3] = {0};

    int local_wordbonus[16];
    memcpy(local_wordbonus, wordbonus[round], 16 * sizeof(int));
    int local_letterbonus[16];
    memcpy(local_letterbonus, letterbonus[round], 16 * sizeof(int));

    if (shuffle_count[round]++ % 100 == 0) {
        for (int i = 15; i > 0; i--) {
            int j = fast_rand() % (i + 1);
            swap(local_wordbonus[i], local_wordbonus[j]);
            swap(local_letterbonus[i], local_letterbonus[j]);
        }
    }

    if (thread_scores == nullptr) {
        thread_scores = new int[flat_trie_next_free]();
    }
    if (thread_word_indices == nullptr) {
        thread_word_indices = new int[flat_trie_next_free]();
    }

    State state;
    state.wordbonusmap = local_wordbonus;
    state.letterbonusmap = local_letterbonus;
    state.scores = thread_scores;

    int attempts = 0;
    while (state.wordcount < 90 && attempts++ < 500) {
        state.wordcount = 0;
        int vowels = 0;

        for (int j = 0; j < 16; j++) {
            char random_letter = letter_sample[fast_rand() % letter_sample_size];
            state.board[j] = random_letter;
            vowels += (random_letter == 'a' || random_letter == 'e' || random_letter == 'i' || random_letter == 'o' || random_letter == 'u');
            state.score_map[j] = letter_scores[random_letter - 'a'];
        }

        if (vowels < 5 || vowels > 10) continue;

        state.quickpass = true;
        state.valid = false;

        for (int j = 0; j < 16 && !state.valid; j++) {
            if (j == 5 || j == 6 || j == 9 || j == 10) {
                words_from_flat(state, 0, j, 0, 0, 1);
                if (state.valid) break;
            }
        }

        if (!state.valid) {
            for (int j = 0; j < 16 && !state.valid; j++) {
                if (j != 5 && j != 6 && j != 9 && j != 10) {
                    words_from_flat(state, 0, j, 0, 0, 1);
                    if (state.valid) break;
                }
            }
        }

        if (!state.valid) continue;

        state.quickpass = false;
        for (int j = 0; j < 16; j++) {
            words_from_flat(state, 0, j, 0, 0, 1);
        }

        for (int j = 0; j < state.wordcount; j++) {
            *(state.score_cleanup[j]) = 0;
        }

        if (state.wordcount < 90) {
            for (int j = 0; j < state.wordcount; j++) {
                free(state.list_words[j]);
            }
        }
    }

    memcpy(out_board, state.board, 16);
    memcpy(out_letterbonusmap, state.letterbonusmap, 16 * sizeof(int));
    memcpy(out_wordbonusmap, state.wordbonusmap, 16 * sizeof(int));
    memcpy(out_list_scores, state.list_scores, state.wordcount * sizeof(int));

    for (int i = 0; i < state.wordcount; i++) {
        out_list_words[i] = state.list_words[i];
    }

    *out_wordcount = state.wordcount;
}

void print_board(char* board) {
    cout << "Board:" << endl;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            cout << (char)toupper(board[i * 4 + j]) << " ";
        }
        cout << endl;
    }
}

void print_bonus_maps(int* letterbonusmap, int* wordbonusmap) {
    cout << "Letter Bonus Map:" << endl;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            cout << letterbonusmap[i * 4 + j] << " ";
        }
        cout << endl;
    }

    cout << "Word Bonus Map:" << endl;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            cout << wordbonusmap[i * 4 + j] << " ";
        }
        cout << endl;
    }
}

void print_words_and_scores(char** list_words, int* list_scores, int wordcount) {
    cout << "Words and Scores (" << wordcount << " words):" << endl;
    for (int i = 0; i < wordcount; i++) {
        string word_upper = list_words[i];
        transform(word_upper.begin(), word_upper.end(), word_upper.begin(), ::toupper);
        cout << word_upper << ": " << list_scores[i] << endl;
    }
}

int main() {
    cout << "Building trie from words.txt..." << endl;
    build_trie_flat();
    cout << "Flat trie memory usage: " << (flat_trie_size * sizeof(int)) / (1024 * 1024) << " MB" << endl;

    fast_srand(time(NULL));
    cout << "Loading letter probabilities from letters.txt..." << endl;
    initialise_probability();

    int wordbonusmap[16];
    int letterbonusmap[16];
    int wordcount = 0;
    char board[16];
    char** list_words = new char*[4096];
    int* list_scores = new int[4096];
    int round = 1;

    typedef std::chrono::high_resolution_clock Clock;
    int numboards = 1000000;
    long long totalwordcount = 0;
    long long total_lookups = 0;

    auto begin = Clock::now();

    cout << "\nGenerating sample boards...\n" << endl;
    for (int i = 0; i < 5; i++) {
        generate_flat(round, board, letterbonusmap, wordbonusmap, &wordcount, list_scores, list_words);
        totalwordcount += wordcount;

        cout << "==========================================" << endl;
        cout << "BOARD " << (i + 1) << ":" << endl;
        cout << "==========================================" << endl;
        print_board(board);
        cout << endl;
        print_bonus_maps(letterbonusmap, wordbonusmap);
        cout << endl;
        print_words_and_scores(list_words, list_scores, wordcount);
        cout << "==========================================" << endl << endl;

        for (int j = 0; j < wordcount; j++) {
            free(list_words[j]);
        }
    }

    total_lookups += thread_lookups;
    thread_lookups = 0;

    cout << "Generating remaining " << (numboards - 5) << " boards..." << endl;

    int remaining = numboards - 5;
    int num_threads = std::thread::hardware_concurrency();
    int boards_per_thread = remaining / num_threads;
    int extra = remaining % num_threads;

    std::vector<std::thread> threads;
    std::vector<long long> thread_wordcounts(num_threads, 0);
    std::vector<long long> thread_lookups_vec(num_threads, 0);

    auto thread_func = [&](int tid, int num_boards) {
        fast_srand(std::hash<std::thread::id>{}(std::this_thread::get_id()) ^ time(NULL));
        thread_lookups = 0;

        long long local_total_wordcount = 0;
        char local_board[16];
        int local_letterbonusmap[16];
        int local_wordbonusmap[16];
        int local_wordcount;
        char* local_list_words[4096];
        int local_list_scores[4096];

        for (int b = 0; b < num_boards; b++) {
            generate_flat(round, local_board, local_letterbonusmap, local_wordbonusmap, &local_wordcount, local_list_scores, local_list_words);
            local_total_wordcount += local_wordcount;

            for (int j = 0; j < local_wordcount; j++) {
                free(local_list_words[j]);
            }
        }

        thread_wordcounts[tid] = local_total_wordcount;
        thread_lookups_vec[tid] = thread_lookups;
    };

    for (int t = 0; t < num_threads; t++) {
        int num = boards_per_thread + (t < extra ? 1 : 0);
        threads.emplace_back(thread_func, t, num);
    }

    for (auto& th : threads) th.join();

    for (int t = 0; t < num_threads; t++) {
        totalwordcount += thread_wordcounts[t];
        total_lookups += thread_lookups_vec[t];
    }

    auto end = Clock::now();
    std::chrono::duration<double> elapsed_secs = end - begin;

    cout << "\n==========================================" << endl;
    cout << "PERFORMANCE SUMMARY" << endl;
    cout << "==========================================" << endl;
    cout << "Boards generated: " << numboards << endl;
    cout << "Time elapsed: " << elapsed_secs.count() << " seconds" << endl;
    cout << "Total lookups: " << total_lookups << endl;
    cout << "Total words found: " << totalwordcount << endl;
    cout << "Average words per board: " << (double)totalwordcount / numboards << endl;
    cout << "Boards per second: " << numboards / elapsed_secs.count() << endl;
    cout << "==========================================" << endl;

    delete[] flat_trie_data;
    delete[] list_words;
    delete[] list_scores;
    delete[] thread_scores;
    delete[] thread_word_indices;

    return 0;
}
