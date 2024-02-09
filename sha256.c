/*
    SHA256 sample code
*/

#include <openssl/evp.h>
#include <stdio.h>
#include <string.h>


// to hex string
char* hexDigest(const void *buf, int len) {
    const unsigned char *cbuf = (const unsigned char*)(buf);
    char *buffer = malloc(1024);
    int p = 0;

    for (int i = 0; i != len; ++i) {
        sprintf(buffer+p, "%.2x", (unsigned int)cbuf[i]);
        p = p + 2;
    }
    buffer[p] = '\0';

    return buffer;
}


int main() {
    // we want to know the hash of "h", "he", "hel", ..., "hello, world"
    // without rehashing the entire string
    char *str = "hello, world";
    char substr[1024] = {};
    char *hex = NULL;
    int n_str = strlen(str);
    
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len;

    // make a SHA256 object and initialize
    EVP_MD_CTX *sha256 = EVP_MD_CTX_new();
    EVP_DigestInit_ex(sha256, EVP_sha256(), NULL);
    
    for (int s_i = 0; s_i != n_str; ++s_i) {
        // update the object given a buffer and length
        // (here we add just one character per update)
        EVP_DigestUpdate(sha256, str + s_i, 1);

        // calculating hash
        // (we need to make a copy of `sha256` for EVP_DigestFinal_ex to use,
        // otherwise `sha256` will be broken)
        EVP_MD_CTX *tmp_sha256 = EVP_MD_CTX_new();
        EVP_MD_CTX_copy_ex(tmp_sha256, sha256);
        EVP_DigestFinal_ex(tmp_sha256, hash, &hash_len);
        EVP_MD_CTX_free(tmp_sha256);
        
        // print hash
        memcpy(substr, str, s_i+1);
        hex = hexDigest(hash, hash_len);
        printf("sha256(\"%s\") = %s\n", substr, hex);
        free(hex);
    }

    EVP_MD_CTX_free(sha256);
}
