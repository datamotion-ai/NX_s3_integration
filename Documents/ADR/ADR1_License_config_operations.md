**What is License.config file?**

It is json file holding the following information used by server manager to access the license details from network optics media server. this file is created during the installation process by letting user enter the following configuration

{
 
 "host": server_url,
 
 "username": username,
 
 "password": encrypted user password,
 
 "local_buffer": local_buffer_size,
 
 "log_level": log_level,
 
 "log_max": log_max,
 
 "max_parallel_upload": thread_max,
 
 "OEM": [encrypted_oems]
 
}

**Why encryption of password and oem?**

1. To make password of users protected from hacker we have to encrypt the password. the encryption mathod we used is 128 byte AES_encrypt having hidden encryption key.
2. To make the plugin work for the registered enterprise we also added supported OEMs encryption to lock for the partnered OEMs

**How license.config file is used?**

ServerManager class read this license.config file, decrypt the password and used this to create session into nx server. this session is also used to get following details

1. License details: to velidate the license expiry
2. OEM details: to validate the oem
3. post event: for notification to the oem
