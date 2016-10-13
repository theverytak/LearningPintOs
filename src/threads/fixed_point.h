// fixed point 연산을 위해 정의한 함수들.

#define F (1 << 14)				//fixed point 1
#define INT_MAX ((1 << 31) - 1)
#define INT_MIN (-(1 << 31))
// x and y denote fixed_point numbers in 17.14 format
// n is an integer


int int_to_fp (int n);						// int to fp
int fp_to_int_round (int x);			// fp to int(반올림)
int fp_to_int (int x);						// fp to int(버림)
int add_fp (int x, int y);				// fp 덧셈
int add_mixed (int x, int n);			// fp, int사이의 덧셈
int sub_fp (int x, int y);				// fp 뺄셈(x-y)
int sub_mixed (int x, int n);			// fp, int사이의 뺄셈(x-y)
int mult_fp (int x, int y);				// fp 곱셈
int mult_mixed (int x, int y);		// fp, int의 곱셈
int div_fp (int x, int y);				// fp 나눗셈(x/y)
int div_mixed (int x, int n);			// fp, int사이의 나눗셈(x/y)


// 아래는 위 함수들 정의 부분임

int int_to_fp (int n) {
	return n * F;
}

int fp_to_int_round (int x) {
	return x / F;
}

int fp_to_int (int x) {
	return x / F;
}

int add_fp (int x, int y) {
	return x + y;
}

int add_mixed (int x, int n) {
	return add_fp (x, int_to_fp (n));
}

int sub_fp (int x, int y) {
	return x - y;
}

int sub_mixed (int x, int n) {
	return sub_fp (x, int_to_fp (n));
}

int mult_fp (int x, int y) {
	return (int) ((long long) x * y / F);
}

int mult_mixed (int x, int y) {
	return mult_fp (x, int_to_fp (y));
}

int div_fp (int x, int y) {
	return (long long) x * F / y;
}

int div_mixed (int x, int n) {
	return div_fp (x, int_to_fp (n));
}
