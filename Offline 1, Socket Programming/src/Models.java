import java.io.Serializable;
import java.time.LocalDateTime;
import java.util.UUID;

public class Models {
    public static class FileMeta implements Serializable {
        public String owner;
        public String filename;
        public boolean isPublic;
        public long sizeKB;
        public String storedFilename; // unique name on server
        public String originalRequestId; // if uploaded to satisfy a request
        public LocalDateTime uploadedAt;

        public FileMeta(String owner, String filename, boolean isPublic, long sizeKB, String storedFilename, String originalRequestId) {
            this.owner = owner;
            this.filename = filename;
            this.isPublic = isPublic;
            this.sizeKB = sizeKB;
            this.storedFilename = storedFilename;
            this.originalRequestId = originalRequestId;
            this.uploadedAt = LocalDateTime.now();
        }
    }

    public static class Request implements Serializable {
        public String id;
        public String fromUser;
        public String toUser; // username or "ALL"
        public String description;
        public LocalDateTime createdAt;

        public Request(String fromUser, String toUser, String description) {
            this.id = UUID.randomUUID().toString();
            this.fromUser = fromUser;
            this.toUser = toUser;
            this.description = description;
            this.createdAt = LocalDateTime.now();
        }
    }

    public static class Message implements Serializable {
        public String fromUser;
        public String content;
        public LocalDateTime at;
        public boolean read;

        public Message(String fromUser, String content) {
            this.fromUser = fromUser;
            this.content = content;
            this.at = LocalDateTime.now();
            this.read = false;
        }
    }
}