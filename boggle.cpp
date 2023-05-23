// boggle.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <ctime>
#include <random>
#include <thread>



using namespace std;

_inline void build_trie(char** trie);
_inline void add_word(const char* word, char** trie);
_inline int search_letter(const char letter, char*** index, int*** locscoreloc);
_inline void words_from(char ** index, int position, int depth, int running_score, int running_multiplier);
_inline void generate();
_inline void initialise_probability();

int lookups = 0;
string letter_sample;
int allocbytes = 0;
int moves[16][8] = { {1, 4, 5, -1, -1, -1, -1, -1},
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
					 {10, 11, 14, -1, -1, -1, -1, -1} };

int letter_scores[26] = {1,2,3,2,1,4,2,4,1,8,5,1,3,1,1,3,10,1,1,1,1,4,4,8,4,10};


_inline void build_trie(char** trie) {
	int count = 0;
	fstream newfile;
	newfile.open("words.txt", ios::in); //open a file to perform read operation using file object
	if (newfile.is_open()) {   //checking whether the file is open
		string tp;
		while (getline(newfile, tp)) { //read data from file object and put it into string.
			add_word(tp.c_str(), trie);
			count++;
		}
		newfile.close(); //close the file object.
	}	
}

_inline void initialise_probability() {
	fstream newfile;
	newfile.open("letters.txt", ios::in); //open a file to perform read operation using file object
	if (newfile.is_open()) {   //checking whether the file is open
		string tp;
		getline(newfile, tp); //read data from file object and put it into string.
		letter_sample = tp;
		newfile.close();
		 //close the file object.
	}
}


_inline void add_word(const char* word, char** trie) {

	char ** current = trie;
	int i = 0;
	char letter;
	
	
	while ((letter = word[i])) {
		char** pos = &(current[int(letter) - 97 + 32]);
		if (!*pos) {
			*pos = (char *) new char*[27]();
			allocbytes += 27;
		}
		current = (char **)*pos;
		i++;
	}

	current[26] = (char*)1;

	return;

}

_inline int search_letter(const char letter, char*** index, int*** loclocscoreloc) {
	int i = 0;
	char** current = *index;
	lookups++;

	char* pos = current[int(letter) - 97 + 32];
	if (!pos) {
		return false;
	}
	current = (char **)pos;
	*index = current;
	i++;

	if (current[26] != (char*)0) {
		*loclocscoreloc = (int**) &current[26];
		return 2;
	}
	else { return true; }

}

int totalwordcount = 0;
char* list_words[1600] = { 0 };
int list_score[1600] = { 0 };
int** score_cleanup[1600] = { 0 };
int score_map[16];
char running_string[16 + 1] = { 0 };  //initalise first to 0, rest made 0 because not specified
char board[17];
int numboards = 10000;
char** trie;
int wordbonus_map[16] = { 2,2,3,1,1,1,1,1,1,1,1,1,1,1,1,1 };
int letterbonus_map[16] = { 1,1,1,2,2,3,3,1,1,1,1,1,1,1,1,1 };
int wordcount = 0;

_inline void words_from(char ** index, int position, int depth, int running_score, int running_multiplier) {

	char letter = board[position];
	running_string[depth] = letter;
	running_string[depth + 1] = '\0';
	depth++;
	running_score = running_score + score_map[position] * letterbonus_map[position];
	running_multiplier = running_multiplier * wordbonus_map[position];
	
	int** locscoreloc = NULL;
	int result = search_letter(letter, &index, &locscoreloc);

	if (depth >= 2) {
		if (!result){
			return; 
		}

		if (result == 2) {
			
			if (*locscoreloc == (int*) 1) {
				list_words[wordcount] = _strdup(running_string);
				list_score[wordcount] = (running_score * running_multiplier) + depth * 2;
				*locscoreloc = &(list_score[wordcount]);
				score_cleanup[wordcount] = locscoreloc;
				(wordcount)++;
			}
		
			else {
				if (**locscoreloc < ((running_score * running_multiplier) + depth * 2)) {
					**locscoreloc = ((running_score * running_multiplier) + depth * 2);
				} 
			}

		}
	}
	char temp = board[position];
	board[position] = '-';

	for (int move: moves[position]) {
		if (move == -1) { break; }
		if (board[move] != '-') {
			words_from (index, move, depth, running_score, running_multiplier);
		}

	}

	board[position] = temp;
}

static unsigned int g_seed;
         
_inline void fast_srand(int seed) {
	g_seed = seed;
}

_inline int fast_rand(void) {
	g_seed = (214013 * g_seed + 2531011);
	return (g_seed >> 16) & 0x7FFF;
}



_inline void generate() {

	for (int j = 0; j < 16; j++) {
		board[j] = letter_sample[fast_rand() % 2350];
		score_map[j] = letter_scores[int(board[j]) - 97 + 32];

	}
	//board[16] = '\0';
	//cout << board << " ";
	


	for (int j = 0; j < 16; j++) {
		words_from(trie, j, 0, 0, 1);
	}

	for (int j = 0; j < 1600; j++) {
		if (!list_words[j]) {
			break;
		}
		*(score_cleanup[j]) = (int*)1;
		//free(list_words[j]);

	}
	memset(score_cleanup, 0, wordcount * sizeof(void*));
	memset(list_words, 0, wordcount * sizeof(void*));
	memset(list_score, 0, wordcount * sizeof(void*));
	totalwordcount += wordcount;
}

int main()
{

	initialise_probability();
	
	trie = new char*[27]();
	build_trie(trie);
	cout << allocbytes << " bytes allocated" << endl;

	typedef std::chrono::high_resolution_clock Clock;


	fast_srand(time(NULL));

	auto begin = Clock::now();

	//thread t(&generate, trie);
	//thread t2(&generate, trie);
	for (int i = 0; i < numboards; i++) {
		generate();
		wordcount = 0;
	}
	//t.join();
	//t2.join();


	
	auto end = Clock::now();
	//cout << totalwordcount << " ";

	//for (int i = 0; i < totalwordcount; i++) {
	//	if (list_score[i] == 0) {
	//		break;
	//	}
	//	cout << " " << list_words[i] << ": " << list_score[i] << " | ";
	//}
	
	std::chrono::duration<double, std::ratio<1, 1>> elapsed_secs = std::chrono::duration_cast<std::chrono::duration<double>>(end - begin);

	cout << 10000 << " random boards solved in "
		<< elapsed_secs.count() << "seconds"  << "with " << lookups << " lookups" << 
		" and "<< totalwordcount << " words " << endl;
	cout << elapsed_secs.count() / totalwordcount;

	
}


