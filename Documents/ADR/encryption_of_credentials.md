## **ADR: Adoption of AES-256 Encryption for Secure File Storage**

### **1. Context**
We want a ADR for encrypting and decrypting files to protect sensitive data like the Nx passwords. 

### **2. Implementation**
The encryption mechanism will be implemented in **C++ using OpenSSL**. 
1. **Generate a random AES-256 key** and Initialization Vector (IV).
2. **Encrypt the file** using AES-256 in CBC mode.
3. **Write the encrypted data to a secure file**.
4. **Decrypt the file when needed**, using the stored AES key and IV.
5. **Securely store and retrieve keys** (avoiding hardcoding).

### **3. Code Implementation**
#### **Encryption & Decryption in C++ (Using OpenSSL)**
```cpp
vector<unsigned char> encrypt(const vector<unsigned char>& plaintext, const vector<unsigned char>& key, const vector<unsigned char>& iv) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    vector<unsigned char> ciphertext(plaintext.size() + AES_BLOCK_SIZE);
    int len, ciphertext_len = 0;

    EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key.data(), iv.data());
    EVP_EncryptUpdate(ctx, ciphertext.data(), &len, plaintext.data(), plaintext.size());
    ciphertext_len += len;
    EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len);
    ciphertext_len += len;

    EVP_CIPHER_CTX_free(ctx);
    ciphertext.resize(ciphertext_len);
    return ciphertext;
}
```

### **4. Security**
- **Key Storage**: AES keys and IVs must be securely stored (e.g., in an encrypted key vault or environment variables).
- **Compliance**: Ensures adherence to security best practices for **GDPR, HIPAA, and SOC2**.
- **Performance**: OpenSSL optimizations ensure minimal impact on execution time.


### **5. Decision Status**
- **Status:** 
- **Date:** 6th February 2025
- **Author:** Emi Roberi
- **Stakeholders:** Brijal & Hari

