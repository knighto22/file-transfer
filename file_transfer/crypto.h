#pragma once
#include <string>
#include <openssl/evp.h>
#include <openssl/rand.h>

/**
 * AES-256-CBC 加密
 * @param plaintext 明文数据
 * @param key 32字节密钥
 * @param iv 16字节初始向量
 * @return 加密后的密文
 */
std::string aesEncrypt(const std::string& plaintext,
    const unsigned char* key,
    const unsigned char* iv) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key, iv);

    std::string ciphertext(plaintext.size() + 16, 0);
    int len = 0, totalLen = 0;

    EVP_EncryptUpdate(ctx, (unsigned char*)&ciphertext[0],
        &len, (unsigned char*)plaintext.c_str(),
        plaintext.size());
    totalLen = len;

    EVP_EncryptFinal_ex(ctx, (unsigned char*)&ciphertext[totalLen], &len);
    totalLen += len;

    EVP_CIPHER_CTX_free(ctx);
    ciphertext.resize(totalLen);
    return ciphertext;
}

/**
 * AES-256-CBC 解密
 * @param ciphertext 密文数据
 * @param key 32字节密钥
 * @param iv 16字节初始向量
 * @return 解密后的明文
 */
std::string aesDecrypt(const std::string& ciphertext,
    const unsigned char* key,
    const unsigned char* iv) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key, iv);

    std::string plaintext(ciphertext.size(), 0);
    int len = 0, totalLen = 0;

    EVP_DecryptUpdate(ctx, (unsigned char*)&plaintext[0],
        &len, (unsigned char*)ciphertext.c_str(),
        ciphertext.size());
    totalLen = len;

    EVP_DecryptFinal_ex(ctx, (unsigned char*)&plaintext[totalLen], &len);
    totalLen += len;

    EVP_CIPHER_CTX_free(ctx);
    plaintext.resize(totalLen);
    return plaintext;
}