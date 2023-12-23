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

__inline void build_trie(char** trie);
__inline void add_word(const char* word, char** trie);
__inline long search_letter(const char letter, char*** index);
__inline void words_from(char ** index, int position, int depth, int running_score, int running_multiplier);
__inline void initialise_probability();
__inline char *strndup(char *str, int chars);
__inline void generate(int round, char* board, int* letterbonusmap, int* wordbonusmap, int* wordcount, int* list_scores, char** list_words);


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


__inline void build_trie(char** trie) {
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

__inline void initialise_probability() {
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


__inline void add_word(const char* word, char** trie) {

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

__inline long search_letter(const char letter, char*** index) {
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
		return (long)&current[26]; //it's a word and returns locscoreloc. scoreloc is just the true flag to indicate
									//that it's a word but contains the scoreloc if the word has been found
	}			
					//score is score in the list of scores, scoreloc is the location of the score
	                //in the list and is stored in the trie
	                //locscoreloc is where in the trie the location is stored

	else { return true; } // not a word but there is a longer word that starts with these letters 

}

char* _list_words[1600];
int _list_scores[1600];
int** score_cleanup[1600];
int score_map[16];
char running_string[16];  //initalise first to 0, rest made 0 because not specified
char _board[17];
char** trie;

int wordbonus[3][16] = { { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },
						{ 2,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },
						{ 3,2,2,1,1,1,1,1,1,1,1,1,1,1,1,1 } };
int* _wordbonusmap;
int letterbonus[3][16] = {{ 3,2,2,1,1,1,1,1,1,1,1,1,1,1,1,1 }, 
							{ 3,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1 }, 
							{ 3,3,2,2,1,1,1,1,1,1,1,1,1,1,1,1 }};
int* _letterbonusmap;
int _wordcount = 0;
int valid = false;


__inline void words_from(char ** index, int position, int depth, int running_score, int running_multiplier) {

	char letter = _board[position];
	running_string[depth] = letter;
	depth++;
	running_score = running_score + score_map[position] * _letterbonusmap[position];
	running_multiplier = running_multiplier * _wordbonusmap[position];
	int finalscore = (running_score * running_multiplier) + depth * 2;
	int** locscoreloc = (int **)search_letter(letter, &index); //score is an integer

	if (depth >= 2) {
		if (!locscoreloc){ //if not a word then locscoreloc contains flag and it is false
			return; 
		}

		if ((long)locscoreloc >= 2) { //if a valid word then locscoreloc contains true flag or scoreloc
			valid = true;  //this letter has at least one valid word
			if (*locscoreloc == (int*)1) { //if locscoreloc still contains true flag (it means word is valid but not already found)
				_list_words[_wordcount] = strndup(running_string, depth); //length omits need for null character; strndup inserts it for us
				_list_scores[_wordcount] = finalscore;
				*locscoreloc = &(_list_scores[_wordcount]);
				score_cleanup[_wordcount] = locscoreloc;
				(_wordcount)++;
			}
		
			else { //otherwise the word has been found already and only the score needs updating
				if (**locscoreloc < finalscore) {
					**locscoreloc = finalscore;
				} 
			}

		}
	}
	char temp = _board[position];
	_board[position] = '-';

	for (int move: moves[position]) {
		if (move == -1) { break; }
		if (_board[move] != '-') {
			words_from (index, move, depth, running_score, running_multiplier);
		}

	}

	_board[position] = temp;
}

static unsigned int g_seed;
         
__inline void fast_srand(int seed) {
	g_seed = seed;
}

__inline int fast_rand(void) {
	g_seed = (214013 * g_seed + 2531011);
	return (g_seed >> 16) & 0x7FFF;
}

int totalscore = 0;

__inline void generate(int round, char* board, int* letterbonusmap, int* wordbonusmap, int* wordcount, int* list_scores, char** list_words) {

	while (_wordcount < 95) {
		_wordcount = 0;
		for (int j = 0; j < 16; j++) {
			_board[j] = letter_sample[fast_rand() % 2350];
			score_map[j] = letter_scores[int(_board[j]) - 97 + 32];

		}

		random_shuffle(begin(wordbonus[round]), end(wordbonus[round]));
		_wordbonusmap = wordbonus[round];

		random_shuffle(begin(letterbonus[round]), end(letterbonus[round]));
		_letterbonusmap = letterbonus[round];

		for (int j = 0; j < 16; j++) {
			words_from(trie, j, 0, 0, 1);
			if (!valid) {
				break;
			}
			valid = false;
		}

		for (int j = 0; j < _wordcount; j++) {
			*(score_cleanup[j]) = (int*)1; //reset scores to true flags
			//free(_list_words[j]);
		}
		
	}

	memcpy(board, _board, 16 * sizeof(char));
	memcpy(letterbonusmap, _letterbonusmap, 16 * sizeof(int));
	memcpy(wordbonusmap, _wordbonusmap, 16 * sizeof(int));
	memcpy(list_scores, _list_scores, _wordcount * sizeof(int));
	memcpy(list_words, _list_words, _wordcount * sizeof(char*));

	*wordcount = _wordcount;
	_wordcount = 0;

}

//void threaded_generate (){
//
	//	for (int i = 0; i < numboards; i++) {
//			//generate();
//		while (_wordcount < 95) {
	//			_wordcount = 0;
	//		}
	//		_wordcount = 0;
//		}
//}

__inline char *strndup(char *str, int chars)
{
	char *buffer;
	int n;

	buffer = (char *)malloc(chars + 1);
	if (buffer)
	{
		for (n = 0; ((n < chars) && (str[n] != 0)); n++) buffer[n] = str[n];
		buffer[n] = 0;
	}

	return buffer;
}

int main()
{
	
	trie = new char*[27]();
	build_trie(trie);
	cout << allocbytes << " bytes allocated" << endl;

	fast_srand(time(NULL));
	initialise_probability();

	int wordbonusmap[16];
	int letterbonusmap[16];
	int wordcount = 0;
	char board[16];
	char* list_words[1600];
	int list_scores[1600];
	int round = 1;
	typedef std::chrono::high_resolution_clock Clock;
	auto begin = Clock::now();
	int numboards = 10000;
	int totalwordcount = 0;

	//thread t(&threaded_generate);
	//thread t2(&threaded_generate);

	for (int i = 0; i < numboards; i++) {
		generate(round, board, letterbonusmap, wordbonusmap, &wordcount, list_scores, list_words);
		totalwordcount += wordcount;
	}

	//t.join();
	//t2.join();
	auto end = Clock::now();

	
	std::chrono::duration<double, std::ratio<1, 1>> elapsed_secs = std::chrono::duration_cast<std::chrono::duration<double>>(end - begin);
	cout << numboards << " random boards solved in "
		<< elapsed_secs.count() << "seconds"  << "with " << lookups << " lookups" <<
		" and "<< totalwordcount << " words " << endl;
	cout << numboards / elapsed_secs.count() << "bps";

	cout << wordcount << " words " << endl;
	//cout << totalwordcount << " words"
	for (int i = 0; i < wordcount; i++) {
		cout << " " << list_words[i] << ": " << list_scores[i] << " | ";
	}

}


