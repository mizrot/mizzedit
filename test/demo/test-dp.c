#include<stdio.h>
#include<string.h>

#define min(a,b) (((a)<(b))?(a):(b))

int main(int argc, const char* argv[]){
  if (argc<3) return -1;
  const char* S = argv[1];
  const char* T = argv[2];
  int n = strlen(S);
  int m = strlen(T);
  int dp[n+1][m+1];
  dp[0][0] = 0;
  for (int i = 0; i <= n; i++) dp[i][0]=i;
  for (int i = 0; i <= m; ++i) dp[0][i]=i;

  for (int n_i = 1; n_i <= n; n_i++){
	for(int m_i = 1; m_i <= m; m_i++){
		int delcost = dp[n_i-1][m_i] + 1;
		int instcost = dp[n_i][m_i-1] + 1;

		int matchcost = 99999999;
		if (S[n_i-1] == T[m_i-1])
			matchcost = dp[n_i-1][m_i-1];
		dp[n_i][m_i]=min(matchcost, min(delcost, instcost));
	}
  }
  for (int n_i = 0; n_i <= n; ++n_i){
	printf("\n");
	for (int m_i = 0; m_i <= m; ++m_i){
	printf("%d ", dp[n_i][m_i]);
	}
  }
}
