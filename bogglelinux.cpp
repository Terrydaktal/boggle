
#include <iostream>
#include <fstream>
#include <cstring>
#include <chrono>
#include <ctime>
#include <random>
#include <thread>
#include <string.h>
#include <stdlib.h>

char* _strdup (const char* s)
{
  size_t slen = strlen(s);
  char* result = (char*)malloc(slen + 1);
  if(result == NULL)
  {
    return NULL;
  }

  memcpy(result, s, slen+1);
  return result;
}



using namespace std;

__inline void build_trie(char** trie);
__inline void add_word(const char* word, char** trie);
__inline long search_letter(const char letter, char*** index);
__inline void words_from(char ** index, int position, int depth, int running_score, int running_multiplier);
__inline void generate();
__inline void initialise_probability();

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
int valid = false;


__inline void words_from(char ** index, int position, int depth, int running_score, int running_multiplier) {

	char letter = board[position];
	running_string[depth] = letter;
	running_string[depth + 1] = '\0';
	depth++;
	running_score = running_score + score_map[position] * letterbonus_map[position];
	running_multiplier = running_multiplier * wordbonus_map[position];
	int finalscore = (running_score * running_multiplier) + depth * 2;
	int** locscoreloc = (int **)search_letter(letter, &index);

	if (depth >= 2) {
		if (!locscoreloc){ //if not a word then locscoreloc contains flag and it is false
			return; 
		}

		if ((long)locscoreloc >= 2) { //if a valid word then locscoreloc contains true flag or scoreloc
			valid = true;  //this letter has at least one valid word
			if (*locscoreloc == (int*)1) { //if locscoreloc still contains true flag (it means word is valid but not already found)
				list_words[wordcount] = _strdup(running_string);
				list_score[wordcount] = finalscore;
				*locscoreloc = &(list_score[wordcount]);
				score_cleanup[wordcount] = locscoreloc;
				(wordcount)++;
			}
		
			else { //otherwise the word has been found already and only the score needs updating
				if (**locscoreloc < finalscore) {
					**locscoreloc = finalscore;
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
         
__inline void fast_srand(int seed) {
	g_seed = seed;
}

__inline int fast_rand(void) {
	g_seed = (214013 * g_seed + 2531011);
	return (g_seed >> 16) & 0x7FFF;
}



inline void generate() {

	for (int j = 0; j < 16; j++) {
		board[j] = letter_sample[fast_rand() % 2350];
		score_map[j] = letter_scores[int(board[j]) - 97 + 32];

	}
	//board[16] = '\0';
	//cout << board << " ";
	

	for (int j = 0; j < 16; j++) {
		words_from(trie, j, 0, 0, 1);
		if (!valid) {
			break;
		}
		valid = false;
	}

	for (int j = 0; j < 1600; j++) {
		if (!list_words[j]) {
			break;
		}
		*(score_cleanup[j]) = (int*)1; //reset scores to true flags
		free(list_words[j]);

	}


	memset(score_cleanup, 0, wordcount * sizeof(void*));
	memset(list_words, 0, wordcount * sizeof(void*));
	memset(list_score, 0, wordcount * sizeof(void*));


	totalwordcount += wordcount;
}

void threaded_generate (){

		for (int i = 0; i < numboards; i++) {
			generate();
			while (wordcount < 95) {
				wordcount = 0;
				generate();
			}
			wordcount = 0;
		}
}

int main()
{
	
	trie = new char*[27]();
	build_trie(trie);
	cout << allocbytes << " bytes allocated" << endl;

	fast_srand(time(NULL));
	initialise_probability();

	typedef std::chrono::high_resolution_clock Clock;
	auto begin = Clock::now();

	//thread t(&threaded_generate);
	//thread t2(&threaded_generate);
	for (int i = 0; i < numboards; i++) {
		while (wordcount < 95) {
			wordcount = 0;
			generate();
		}
		wordcount = 0;
	}
	//t.join();
	//t2.join();


	
	auto end = Clock::now();

	
	std::chrono::duration<double, std::ratio<1, 1>> elapsed_secs = std::chrono::duration_cast<std::chrono::duration<double>>(end - begin);
	cout << numboards << " random boards solved in "
		<< elapsed_secs.count() << "seconds"  << "with " << lookups << " lookups" <<
		" and "<< totalwordcount << " words " << endl;
	cout << numboards / elapsed_secs.count() << "bps";

	//cout << totalwordcount << " words";
	//for (int i = 0; i < totalwordcount; i++) {
	//	if (list_score[i] == 0) {
	//		break;
	//	}
	//	cout << " " << list_words[i] << ": " << list_score[i] << " | ";
    //
	//}
}


