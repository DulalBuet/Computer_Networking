import java.util.*;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicLong;

public class ServerState {
    public final Map<String, ClientHandler> online = new ConcurrentHashMap<>();
    public final Set<String> knownUsers = Collections.synchronizedSet(new HashSet<>());
    public final Map<String, List<Models.FileMeta>> files = new ConcurrentHashMap<>();
    public final Map<String, Models.Request> requests = new ConcurrentHashMap<>();
    public final Map<String, List<Models.Message>> messages = new ConcurrentHashMap<>();
    private final AtomicLong bufferOccupiedKB = new AtomicLong(0);
    public boolean canAcceptNewFile(long newFileSizeKB) {
        return bufferOccupiedKB.get() + newFileSizeKB <= Config.MAX_BUFFER_SIZE_KB;
    }
    public void addToBuffer(long kb) {
        bufferOccupiedKB.addAndGet(kb);
    }
    public void removeFromBuffer(long kb) {
        bufferOccupiedKB.addAndGet(-kb);
    }
    public String generateStoredFilename(String owner, String filename) {
        return owner + "__" + UUID.randomUUID().toString() + "__" + filename;
    }
}