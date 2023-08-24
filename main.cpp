#include <iostream>
#include "Table.h"
#include <map>

using namespace std;

int main() {
    register_hashes();

    int t = clock();

    Table table;

    for (int i = 1; i <= 5000000; i++)
        table[i] = 1;

    int sum = 0;

    for (int i = 1; i <= 5000000; i++)
        sum += table[i].into<int>();

    cout << sum << endl;

    cout << (double)(clock() - t) / CLOCKS_PER_SEC << endl;

}
