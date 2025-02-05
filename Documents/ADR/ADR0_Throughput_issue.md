**Throughput issue:**

Currently, the file is written into the local system and another thread uploads the file sequentially making the pile of files to be uploaded into the s3 holding the huge memory into the local system causing the crash and memory out issue.

**Current implementation:**

The s3IODevice class writes the recording into a file stored into local “C:\Windows\Temp\Nx Witness” folder. It also added file path into json file having all files need to be uploaded into the s3 server.

The s3Client class has thread function named “s3Client::fileUploadThread()” which get the filename by reading the json file from function s3Client::getNextFileToUpload(), need to be uploaded into s3, sequentially and read that file data and upload that into the s3 server.

**Root cause:**

As the file is uploaded sequentially, when there is more number of camera for ex. 51 cameras, this will create 51 files recording into local system/ minutes. Makes all 51 recording to be uploaded into s3 server immediately. But due to the sequential upload functionality, it creates a huge pile of about 3000 files to be uploaded after some time causing the throughput issue.

**Solution:**

1. **Thread pool**

   Parallel uploading thread that handles multiple file upload into s3 server. This will resolve the pile of files created into the local system. For this we create “ThreadPool” class dynamically create threads according to the number of files need to be uploaded into the server. maximum thread it can create to handle CPU usage and network bandwidth is configured through licence.config file.

   **Why Max thread creation setting configuration?**
   To handle the CPU consumption issue and network bandwidth to upload max number of files smoothly into the s3.

3. **Notification into network optics** 

   When the network bandwidth is very low increasing number of parallel upload solution will not work as the files into storage are overloaded.

   Solution:

   Notify the user on network optics that the storage is overloaded. For example, when storage exceeds 200 files notify the user once. For 400 files notify the user every minute. For 600 files notify the user every second.

   This makes the user aware that the network which the user is operating is not working smoothly for uploading the files. 

**Future Scope:**

1. Instead of manually configuring the max\_thread, dynamically updating the max\_thread on basis of the number of files needed to be uploaded into json queue and uploading speed.

