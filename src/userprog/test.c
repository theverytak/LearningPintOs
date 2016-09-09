#include <stdio.h>
#include <stdlib.h>
#include <string.h>
strlcpy (char *dst, const char *src, size_t size) 
{
  size_t src_len;

  src_len = strlen (src);
  if (size > 0) 
    {
      size_t dst_len = size - 1;
      if (src_len < dst_len)
        dst_len = src_len;
      memcpy (dst, src, dst_len);
      dst[dst_len] = '\0';
    }
  return src_len;
}
int main() {

	char *string = "hello";
	char *temp = (char*)malloc(sizeof(char) * strlen(string));
	printf("strlen(string) : %d\n", strlen(string));
	strlcpy(temp, string, strlen(string) + 1);
	printf("temp : %s\n", temp);
	printf("strlen(temp) : %d\n", strlen(temp));
	if(temp[5] == '\0')
		printf("hi\n");
	return 0;
}
