int g(int N) {
  int s = 0;
  for (int i = 0; i < N; ++i) {
    for (int j = 0; j < N; ++j) {
      int k = 2*j + 3;
      s += k;
    }
  }
  return s;
}
