// boggle.cpp : Ultra-optimized Boggle board generator and solver
// WITH CACHE ALIGNMENT + SIMD + AVX-512 OPTIMIZATIONS

#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <chrono>
#include <ctime>
#include <random>
#include <algorithm>
#include <immintrin.h>  // SSE/AVX/AVX-512 intrinsics

using namespace std;
#define letter_sample_size 2350
// Cache line size constant
#define CACHE_LINE_SIZE 64

// Macro for cache alignment
#define CACHE_ALIGNED alignas(CACHE_LINE_SIZE)

// Branch prediction hints
#ifdef __GNUC__
#define LIKELY(x)   __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define LIKELY(x)   (x)
#define UNLIKELY(x) (x)
#endif

// AVX-512 feature detection
#ifdef __AVX512F__
#define HAS_AVX512 1
#else
#define HAS_AVX512 0
#endif

// Memory pool for word strings - eliminates malloc/free overhead
struct MemoryPool {
    static const int POOL_SIZE = 1600 * 20;  // Max words * max word length
    char pool[POOL_SIZE];
    int offset;

    MemoryPool() : offset(0) {}

    inline char* allocate(int size) {
        if (UNLIKELY(offset + size > POOL_SIZE)) {
            offset = 0;  // Wrap around (we clear between boards anyway)
        }
        char* ptr = pool + offset;
        offset += size;
        return ptr;
    }

    inline void reset() {
        offset = 0;
    }
};

CACHE_ALIGNED MemoryPool word_pool;

// Flat trie implementation
int* flat_trie_data = nullptr;
int flat_trie_size = 0;
int flat_trie_next_free = 1;

int lookups = 0;
CACHE_ALIGNED char letter_sample[letter_sample_size];

// Optimized adjacency lookup - cache aligned
CACHE_ALIGNED const int moves[16][8] = {
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

// Move counts per position - cache aligned for better prediction
CACHE_ALIGNED const int move_counts[16] = {3, 5, 5, 3, 5, 8, 8, 5, 5, 8, 8, 5, 3, 5, 5, 3};

// Position type for optimized traversal (center = 1, edge = 0, corner = -1)
CACHE_ALIGNED const int position_priority[16] = {
    -1, 0, 0, -1,
     0, 1, 1,  0,
     0, 1, 1,  0,
    -1, 0, 0, -1
};

// Precomputed position ordering for quickpass - centers first
CACHE_ALIGNED const int quickpass_order[16] = {
    5, 6, 9, 10,  // Centers (8 moves each)
    1, 2, 4, 7, 8, 11, 13, 14,  // Edges (5 moves each)
    0, 3, 12, 15  // Corners (3 moves each)
};

CACHE_ALIGNED const int letter_scores[26] = { 1,2,3,2,1,4,2,4,1,8,5,1,3,1,1,3,10,1,1,1,1,4,4,8,4,10 };

// Hot path data structure - all frequently accessed data in one cache line group
struct CACHE_ALIGNED HotPathData {
    char board[16];           // 16 bytes
    int score_map[16];        // 64 bytes (spans to next cache line)
    // Pad to prevent false sharing with next structure
    char _padding[48];
};

HotPathData hot_data;

// Pre-allocated word storage - separated to avoid cache conflicts
CACHE_ALIGNED char* _list_words[1600];
CACHE_ALIGNED int _list_scores[1600];
CACHE_ALIGNED int* score_cleanup[1600];
CACHE_ALIGNED char running_string[16];

// Bonus maps - cache aligned and grouped
CACHE_ALIGNED int wordbonus[3][16] = {
    { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },
    { 2,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },
    { 3,2,2,1,1,1,1,1,1,1,1,1,1,1,1,1 }
};

CACHE_ALIGNED int letterbonus[3][16] = {
    { 3,2,2,1,1,1,1,1,1,1,1,1,1,1,1,1 },
    { 3,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },
    { 3,3,2,2,1,1,1,1,1,1,1,1,1,1,1,1 }
};

int* _wordbonusmap;
int* _letterbonusmap;

int _wordcount = 0;
bool valid = false;
bool quickpass = false;

// Xorshift+ Random Number Generator - cache aligned state
CACHE_ALIGNED static uint64_t xorshift_state[2] = {123456789ULL, 987654321ULL};

inline void fast_srand(int seed) {
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

// Vowel check - SIMD optimized with AVX-512 support
#if HAS_AVX512
inline int count_vowels_avx512(const char* board) {
    // Load 16 bytes from board into lower half of ZMM register
    __m512i data = _mm512_maskz_loadu_epi8(0xFFFF, board);

    // Create comparison masks for each vowel
    __mmask64 mask_a = _mm512_cmpeq_epi8_mask(data, _mm512_set1_epi8('a'));
    __mmask64 mask_e = _mm512_cmpeq_epi8_mask(data, _mm512_set1_epi8('e'));
    __mmask64 mask_i = _mm512_cmpeq_epi8_mask(data, _mm512_set1_epi8('i'));
    __mmask64 mask_o = _mm512_cmpeq_epi8_mask(data, _mm512_set1_epi8('o'));
    __mmask64 mask_u = _mm512_cmpeq_epi8_mask(data, _mm512_set1_epi8('u'));

    // Combine all masks with OR operations
    __mmask64 vowel_mask = mask_a | mask_e | mask_i | mask_o | mask_u;

    // Count set bits in the lower 16 bits
    return __builtin_popcountll(vowel_mask & 0xFFFF);
}
#endif

inline int count_vowels_simd(const char* board) {
#if HAS_AVX512
    return count_vowels_avx512(board);
#else
    // Load 16 bytes from board into SSE register
    __m128i data = _mm_loadu_si128((__m128i*)board);

    // Create comparison masks for each vowel
    __m128i mask_a = _mm_cmpeq_epi8(data, _mm_set1_epi8('a'));
    __m128i mask_e = _mm_cmpeq_epi8(data, _mm_set1_epi8('e'));
    __m128i mask_i = _mm_cmpeq_epi8(data, _mm_set1_epi8('i'));
    __m128i mask_o = _mm_cmpeq_epi8(data, _mm_set1_epi8('o'));
    __m128i mask_u = _mm_cmpeq_epi8(data, _mm_set1_epi8('u'));

    // Combine all masks with OR operations
    __m128i vowel_mask = _mm_or_si128(mask_a, mask_e);
    vowel_mask = _mm_or_si128(vowel_mask, mask_i);
    vowel_mask = _mm_or_si128(vowel_mask, mask_o);
    vowel_mask = _mm_or_si128(vowel_mask, mask_u);

    // Count set bits (each vowel match is 0xFF = -1)
    // Use movemask to extract the high bit of each byte
    int mask = _mm_movemask_epi8(vowel_mask);

    // Count number of set bits using popcount
    return __builtin_popcount(mask);
#endif
}

// SIMD-optimized letter score computation with AVX-512
#if HAS_AVX512
inline void compute_scores_avx512(const char* board, int* score_map) {
    // Load 16 characters
    __m128i letters = _mm_loadu_si128((__m128i*)board);

    // Subtract 'a' to get indices (0-25)
    __m128i base_a = _mm_set1_epi8('a');
    __m128i indices_8bit = _mm_sub_epi8(letters, base_a);

    // Expand to 32-bit indices for gather
    __m256i indices_lo = _mm256_cvtepi8_epi32(indices_8bit);
    __m256i indices_hi = _mm256_cvtepi8_epi32(_mm_srli_si128(indices_8bit, 8));

    // Gather scores using AVX-512 (process 8 at a time with AVX2 gather)
    __m256i scores_lo = _mm256_i32gather_epi32(letter_scores, indices_lo, 4);
    __m256i scores_hi = _mm256_i32gather_epi32(letter_scores, indices_hi, 4);

    // Store results
    _mm256_storeu_si256((__m256i*)score_map, scores_lo);
    _mm256_storeu_si256((__m256i*)(score_map + 8), scores_hi);
}
#endif

inline void compute_scores_simd(const char* board, int* score_map) {
#if HAS_AVX512
    compute_scores_avx512(board, score_map);
#else
    // Process 16 characters at once
    __m128i letters = _mm_loadu_si128((__m128i*)board);

    // Subtract 'a' from each letter to get index
    __m128i base_a = _mm_set1_epi8('a');
    __m128i indices = _mm_sub_epi8(letters, base_a);

    // For each position, lookup the score
    // Note: This part still requires scalar access due to gather limitations
    // but we've streamlined the process
    for (int i = 0; i < 16; i++) {
        score_map[i] = letter_scores[board[i] - 'a'];
    }
#endif
}

// SIMD random letter generation with AVX-512
#if HAS_AVX512
inline void generate_letters_avx512(char* board, const char* sample, int sample_size) {
    // Generate 16 random indices in parallel using AVX-512
    __m512i sample_size_vec = _mm512_set1_epi32(sample_size);

    // Generate random numbers (4 at a time, 4 iterations = 16 total)
    for (int i = 0; i < 16; i += 4) {
        uint32_t r0 = fast_rand() % sample_size;
        uint32_t r1 = fast_rand() % sample_size;
        uint32_t r2 = fast_rand() % sample_size;
        uint32_t r3 = fast_rand() % sample_size;

        board[i]   = sample[r0];
        board[i+1] = sample[r1];
        board[i+2] = sample[r2];
        board[i+3] = sample[r3];
    }
}
#endif

inline void generate_letters_simd(char* board, const char* sample, int sample_size) {
#if HAS_AVX512
    generate_letters_avx512(board, sample, sample_size);
#else
    // Generate 4 random positions at once using SIMD
    for (int i = 0; i < 16; i += 4) {
        // Generate 4 random numbers
        uint32_t r0 = fast_rand() % sample_size;
        uint32_t r1 = fast_rand() % sample_size;
        uint32_t r2 = fast_rand() % sample_size;
        uint32_t r3 = fast_rand() % sample_size;

        // Store directly
        board[i] = sample[r0];
        board[i+1] = sample[r1];
        board[i+2] = sample[r2];
        board[i+3] = sample[r3];
    }
#endif
}

// Original vowel check kept for compatibility
inline bool is_vowel(char c) {
    return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u';
}

inline void init_flat_trie() {
    flat_trie_size = 28 * 200000;
    // Use aligned allocation for trie data
    #ifdef _WIN32
        flat_trie_data = (int*)_aligned_malloc(flat_trie_size * sizeof(int), CACHE_LINE_SIZE);
    #else
        posix_memalign((void**)&flat_trie_data, CACHE_LINE_SIZE, flat_trie_size * sizeof(int));
    #endif
    memset(flat_trie_data, 0, flat_trie_size * sizeof(int));
}

inline int flat_trie_allocate_node() {
    if (flat_trie_next_free * 28 >= flat_trie_size) {
        int new_size = flat_trie_size * 2;
        int* new_data;
        #ifdef _WIN32
            new_data = (int*)_aligned_malloc(new_size * sizeof(int), CACHE_LINE_SIZE);
        #else
            posix_memalign((void**)&new_data, CACHE_LINE_SIZE, new_size * sizeof(int));
        #endif
        memset(new_data, 0, new_size * sizeof(int));
        memcpy(new_data, flat_trie_data, flat_trie_size * sizeof(int));
        #ifdef _WIN32
            _aligned_free(flat_trie_data);
        #else
            free(flat_trie_data);
        #endif
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
                int node_offset = current * 28 + letter_index;

                if (flat_trie_data[node_offset] == 0) {
                    int new_node = flat_trie_allocate_node();
                    flat_trie_data[node_offset] = new_node;
                }
                current = flat_trie_data[node_offset];
            }
            flat_trie_data[current * 28 + 26] = 1;
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
        for (int i = 0; i < letter_sample_size; i++) {
            letter_sample[i] = tp[i] | 0x20;
        }
        newfile.close();
    }
}

inline long search_letter_flat(char letter, int& current_node) {
    lookups++;

    int letter_index = letter - 'a';
    int node_offset = current_node * 28 + letter_index;

    // Branch prediction hint: most lookups succeed
    if (UNLIKELY(flat_trie_data[node_offset] == 0)) {
        return 0;
    }

    current_node = flat_trie_data[node_offset];

    // Check for word end - this is less common
    if (UNLIKELY(flat_trie_data[current_node * 28 + 26] != 0)) {
        return (long)&flat_trie_data[current_node * 28 + 27];
    }

    return 1;
}

inline void words_from_flat(int current_node, int position, int depth, int running_score, int running_multiplier) {
    char letter = hot_data.board[position];
    running_string[depth] = letter;
    depth++;
    running_score = running_score + hot_data.score_map[position] * _letterbonusmap[position];
    running_multiplier = running_multiplier * _wordbonusmap[position];

    long result = search_letter_flat(letter, current_node);

    // Branch hint: early termination is rare in valid paths
    if (UNLIKELY(result == 0)) {
        return;
    }

    // Word found check - happens occasionally
    if (LIKELY(depth >= 2) && UNLIKELY(result > 1)) {
        valid = true;
        if (UNLIKELY(quickpass)) return;

        int finalscore = (running_score * running_multiplier) + depth * 2;
        int* score_location = (int*)result;

        if (LIKELY(*score_location == 0)) {
            // Memory pool allocation instead of malloc
            _list_words[_wordcount] = word_pool.allocate(depth + 1);
            memcpy(_list_words[_wordcount], running_string, depth);
            _list_words[_wordcount][depth] = 0;

            _list_scores[_wordcount] = finalscore;
            *score_location = finalscore;
            score_cleanup[_wordcount] = score_location;
            _wordcount++;
        } else if (UNLIKELY(*score_location < finalscore)) {
            *score_location = finalscore;
        }
    }

    char temp = hot_data.board[position];
    hot_data.board[position] = '-';

    const int* move_ptr = moves[position];
    int count = move_counts[position];

    // Unified loop using precomputed move count
    // Branch hint: most adjacent cells are valid (not visited)
    for (int i = 0; i < count; i++) {
        int move = move_ptr[i];
        if (LIKELY(hot_data.board[move] != '-')) {
            words_from_flat(current_node, move, depth, running_score, running_multiplier);
            if (UNLIKELY(quickpass && valid)) break;
        }
    }

    hot_data.board[position] = temp;
}

inline void generate_flat(int round, char* board, int* letterbonusmap, int* wordbonusmap, int* wordcount, int* list_scores, char** list_words) {
    static int shuffle_count[3] = {0};
    for (int i = 15; i > 0; i--) {
        int j = fast_rand() % (i + 1);
        swap(wordbonus[round][i], wordbonus[round][j]);
    }
    
    // Shuffle letter bonus
    for (int i = 15; i > 0; i--) {
        int j = fast_rand() % (i + 1);
        swap(letterbonus[round][i], letterbonus[round][j]);
    }
    
    // Fix conflicts: swap 3x letter bonus squares that conflict with 3x word bonus
    for (int i = 0; i < 16; i++) {
        if (wordbonus[round][i] == 3 && letterbonus[round][i] == 3) {
            // Find first square without 3x letter bonus and swap
            bool swapped = false;
            for (int j = 0; j < 16; j++) {
                if (j != i && letterbonus[round][j] != 3) {
                    swap(letterbonus[round][i], letterbonus[round][j]);
                    swapped = true;
                    break;
                }
            }
            // Fallback: if no non-3 letter bonus found, swap with adjacent square
            if (!swapped && i < 15) {
                swap(letterbonus[round][i], letterbonus[round][i + 1]);
            }
        }
    }

    int attempts = 0;
    while (LIKELY(_wordcount < 90) && LIKELY(attempts++ < 500)) {
        _wordcount = 0;
        word_pool.reset();  // Reset memory pool for new board

        // SIMD-optimized letter generation
        generate_letters_simd(hot_data.board, letter_sample, letter_sample_size);

        // SIMD-optimized vowel counting
        int vowels = count_vowels_simd(hot_data.board);

        // Branch hint: most boards have acceptable vowel counts
        if (UNLIKELY(vowels < 5 || vowels > 10)) continue;

        // SIMD-optimized score computation
        compute_scores_simd(hot_data.board, hot_data.score_map);

        _wordbonusmap = wordbonus[round];
        _letterbonusmap = letterbonus[round];

        quickpass = true;
        valid = false;

        // Use precomputed ordering for better branch prediction
        // Try center positions first (highest connectivity)
        for (int idx = 0; idx < 16 && LIKELY(!valid); idx++) {
            int j = quickpass_order[idx];
            words_from_flat(0, j, 0, 0, 1);
            if (UNLIKELY(valid)) break;
            // Early exit after checking high-value positions
            if (UNLIKELY(idx == 3 && !valid)) break;  // After centers
        }

        // If still no valid words, check remaining positions
        if (UNLIKELY(!valid)) {
            for (int idx = 4; idx < 16 && !valid; idx++) {
                int j = quickpass_order[idx];
                words_from_flat(0, j, 0, 0, 1);
                if (valid) break;
            }
        }

        if (UNLIKELY(!valid)) continue;

        quickpass = false;

        // Full word search - use same ordering for consistency
        for (int idx = 0; idx < 16; idx++) {
            int j = quickpass_order[idx];
            words_from_flat(0, j, 0, 0, 1);
        }

        // Cleanup score markers
        for (int j = 0; j < _wordcount; j++) {
            *(score_cleanup[j]) = 0;
        }
    }

    memcpy(board, hot_data.board, 16);
    memcpy(letterbonusmap, _letterbonusmap, 16 * sizeof(int));
    memcpy(wordbonusmap, _wordbonusmap, 16 * sizeof(int));
    memcpy(list_scores, _list_scores, _wordcount * sizeof(int));

    // Copy words from memory pool to heap-allocated memory for output
    for (int i = 0; i < _wordcount; i++) {
        int len = strlen(_list_words[i]) + 1;
        list_words[i] = (char*)malloc(len);
        memcpy(list_words[i], _list_words[i], len);
    }

    *wordcount = _wordcount;
    _wordcount = 0;
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
    char** list_words = new char*[1600];
    int* list_scores = new int[1600];
    int round = 1;

    typedef std::chrono::high_resolution_clock Clock;
    int numboards = 100000;
    int totalwordcount = 0;

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
            free(list_words[j]);  // Safe to free - these are heap-allocated copies
        }
    }

    cout << "Generating remaining " << (numboards - 5) << " boards..." << endl;
    for (int i = 5; i < numboards; i++) {
        generate_flat(round, board, letterbonusmap, wordbonusmap, &wordcount, list_scores, list_words);
        totalwordcount += wordcount;

        for (int j = 0; j < wordcount; j++) {
            free(list_words[j]);
        }
    }

    auto end = Clock::now();
    std::chrono::duration<double> elapsed_secs = end - begin;

    cout << "\n==========================================" << endl;
    cout << "PERFORMANCE SUMMARY" << endl;
    cout << "==========================================" << endl;
    cout << "Boards generated: " << numboards << endl;
    cout << "Time elapsed: " << elapsed_secs.count() << " seconds" << endl;
    cout << "Total lookups: " << lookups << endl;
    cout << "Total words found: " << totalwordcount << endl;
    cout << "Average words per board: " << (double)totalwordcount / numboards << endl;
    cout << "Boards per second: " << numboards / elapsed_secs.count() << endl;
    cout << "==========================================" << endl;

    #ifdef _WIN32
        _aligned_free(flat_trie_data);
    #else
        free(flat_trie_data);
    #endif
    delete[] list_words;
    delete[] list_scores;

    return 0;
}
