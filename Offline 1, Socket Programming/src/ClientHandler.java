import java.io.*;
import java.net.Socket;
import java.nio.file.DirectoryStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.LocalDateTime;
import java.util.*;

public class ClientHandler extends Thread {
    private final Socket socket;
    private final ServerState state;
    private String username = null;
    private BufferedReader in;
    private BufferedWriter out;
    public ClientHandler(Socket socket, ServerState state){
        this.socket = socket;
        this.state = state;
    }

    @Override
    public void run(){
        try{
            in = new BufferedReader(new InputStreamReader(socket.getInputStream()));
            out = new BufferedWriter(new OutputStreamWriter(socket.getOutputStream()));
            String line = in.readLine();
            if(line == null){
                closeSilently();
                return;
            }

            String[] toks = line.split(" ", 2);
            if(!toks[0].equals(Protocol.LOGIN) || toks.length<2){
                sendLine(Protocol.LOGIN_FAIL + " Missing username\n");
            }

            String requested = toks[1].trim();
            synchronized (state){
                if(state.online.containsKey(requested)){
                    sendLine(Protocol.LOGIN_FAIL + " Username already online\n");
                    closeSilently();
                    return;
                }
                this.username = requested;
                state.online.put(username, this);
                state.knownUsers.add(username);
            }

            Path userDir = Paths.get("data", username);
            Files.createDirectories(userDir);
            sendLine(Protocol.LOGIN_OK + " Welcome " + username + "\n");

            while ((line = in.readLine()) != null){
                if(line.trim().isEmpty())continue;
                handleCommand(line);
            }
        } catch (IOException e) {
            throw new RuntimeException(e);
        }finally {
            logoutCleanup();
        }
    }

    private void handleCommand(String line)throws IOException{
        String[] parts = line.split(" ", 2);
        String cmd = parts[0];
        String args = parts.length>1?parts[1]:"";
        switch (cmd) {
            case Protocol.LIST_USERS -> handleListUsers();
            case Protocol.LIST_FILES -> handleListFiles(args);
            case Protocol.UPLOAD -> handleUpload(args);
            case Protocol.DOWNLOAD -> handleDownload(args);
            case Protocol.REQUEST -> handleRequest(args);
            case Protocol.MESSAGES -> handleMessages();
            case Protocol.HISTORY -> handleHistory();
            case Protocol.LOGOUT -> {
                sendLine("Bye\n");
                closeSilently();
            }
            default -> sendLine("Error unknown command\n");
        }
    }

    private void handleListUsers() throws IOException{
        StringBuilder sb = new StringBuilder();
        synchronized ( (state.knownUsers)){
            for(String u: state.knownUsers){
                boolean online = state.online.containsKey(u);
                sb.append(u).append(online?" [online]":" [offline]").append("\n");
            }
        }
        sendLine(sb.toString());
    }

    private void handleListFiles(String args) throws IOException{
        String mode = args.trim();
        StringBuilder sb = new StringBuilder();

        if(mode.equalsIgnoreCase("MINE") || mode.isEmpty()){
            List<Models.FileMeta> list = state.files.getOrDefault(username, Collections.emptyList());
            for(Models.FileMeta f: list){
                sb.append(f.filename).append(" \t").append(f.isPublic?"public":"private").append("\n");
            }
        } else if (mode.equalsIgnoreCase("PUBLIC")) {
            for(Map.Entry<String, List<Models.FileMeta>> e: state.files.entrySet()){
                for(Models.FileMeta f: e.getValue()){
                    if(f.isPublic){
                        sb.append(e.getKey()).append("/").append(f.filename).append("\n");
                    }
                }
            }
        } else {
            String who = mode;
            for(Models.FileMeta f: state.files.getOrDefault(who, Collections.emptyList())){
                if(f.isPublic || who.equals(username)) sb.append(f.filename).append(" \t").append(f.isPublic?"public":"private").append("\n");
            }
        }
        sendLine(sb.toString());
    }

    private void handleUpload(String args) throws IOException{
        String[] toks = args.split(" ");
        if(toks.length<3){
            sendLine(Protocol.UPLOAD_FAIL + " invalid args\n");
            return;
        }

        String filename = toks[0];
        long filesizeKB = Long.parseLong(toks[1]);
        String access = toks[2];
        String requestID = (toks.length>=4?toks[3]:"-");

        synchronized (state){
            if(!state.canAcceptNewFile(filesizeKB)){
                sendLine(Protocol.UPLOAD_FAIL + " buffer_full\n");
                return;
            }
            state.addToBuffer(filesizeKB);
        }

        long chunkSizeKB = Config.MIN_CHUNK_SIZE_KB + (long)(Math.random()*(Config.MAX_CHUNK_SIZE_KB - Config.MIN_CHUNK_SIZE_KB + 1));
        String fileID = UUID.randomUUID().toString();
        sendLine(Protocol.UPLOAD_CHUNK_INFO + " " + fileID + " " + chunkSizeKB + "\n");

        Path tempDir = Paths.get("buffer", fileID);
        Files.createDirectories(tempDir);
        long accumulatedKB = 0;
        OutputStream dummyOut = null;
        boolean failed = false;

        try {
            DataInputStream dis = new DataInputStream(socket.getInputStream());
            while (true){
                String ctrl = in.readLine();
                if(ctrl == null){
                    failed = true;
                    break;
                }

                if(ctrl.equals(Protocol.COMPLETE)){
                    break;
                }

                if(!ctrl.startsWith("CHUNK")){
                    failed = true;
                    break;
                }

                String []c = ctrl.split(" ");
                int len = Integer.parseInt(c[1]);
                byte[] buf = new byte[len];
                int read = 0;

                while (read < len){
                    int r = dis.read(buf, read, len-read);
                    if(r<=0){
                        failed = true;
                        break;
                    }
                    read += r;
                }

                if(failed) break;

                Path chunkFile = tempDir.resolve(UUID.randomUUID().toString() + ".chunk");
                Files.write(chunkFile, buf);
                accumulatedKB += (len + 1023)/1024;
                sendLine(Protocol.ACK + "\n");
            }

            if(failed){
                sendLine(Protocol.UPLOAD_FAIL + " transmission_error\n");
                deleteDirectory(tempDir);
                synchronized (state){
                    state.removeFromBuffer(filesizeKB);
                    return;
                }
            }

            if(accumulatedKB == filesizeKB){
                Path userDir = Paths.get("data", username);
                Files.createDirectories(userDir);
                String stored = state.generateStoredFilename(username, filename);
                Path outFile = userDir.resolve(stored);

                try(OutputStream os = Files.newOutputStream(outFile)){
                    DirectoryStream<Path> ds = Files.newDirectoryStream(tempDir);
                    for(Path p: ds){
                        byte[] d = Files.readAllBytes(p);
                        os.write(d);
                    }
                }

                boolean isPublic = access.equalsIgnoreCase("public");
                Models.FileMeta meta = new Models.FileMeta(username, filename, isPublic, filesizeKB, stored, requestID.equals("-")?null:requestID);
                state.files.computeIfAbsent(username, k->Collections.synchronizedList(new ArrayList<>())).add(meta);

                if(meta.originalRequestId != null){
                    Models.Request req = state.requests.get(meta.originalRequestId);
                    if(req != null){
                        String to = req.fromUser;
                        Models.Message msg = new Models.Message(username, "Requested file'" + filename + "' uploaded for request" + req.id);
                        state.messages.computeIfAbsent(to, k->Collections.synchronizedList(new ArrayList<>())).add(msg);
                        ClientHandler ch = state.online.get(to);
                        if(ch != null){
                            ch.sendLine("MESSAGE " + username + " uploaded requested file " + filename + "\n");

                        }
                    }
                    sendLine(Protocol.UPLOAD_SUCCESS + "\n");
                    writeHistory(username, filename, "UPLOAD", "SUCCESS");
                }else{
                    sendLine(Protocol.UPLOAD_FAIL + " size_mismatch\n");
                    deleteDirectory(tempDir);
                    writeHistory(username, filename, "UPLOAD", "FAILED");
                }
            }
        }catch (Exception e){
            sendLine(Protocol.UPLOAD_FAIL + " exception\n");
            deleteDirectory(tempDir);
            writeHistory(username, filename, "UPLOAD", "FAILED");
        } finally {
            synchronized (state) {
                state.removeFromBuffer(filesizeKB);
            }
        }
    }

    private void handleDownload(String args) throws IOException{
        String[] toks = args.split(" ");
        if (toks.length < 2) {
            sendLine("ERROR invalid args\n");
            return;
        }

        String owner = toks[0];
        String filename = toks[1];

        Models.FileMeta chosen = null;
        for (Models.FileMeta f: state.files.getOrDefault(owner, Collections.emptyList())) {
            if (f.filename.equals(filename)) {
                chosen = f;
                break;
            }
        }
        if (chosen == null) {
            sendLine("ERROR file_not_found\n");
            return;
        }
        if (!chosen.isPublic && !owner.equals(username)) {
            sendLine("ERROR permission_denied\n");
            return;
        }

        Path fpath = Paths.get("data", owner, chosen.storedFilename);

        if (!Files.exists(fpath)) {
            sendLine("ERROR file_missing\n");
            return;
        }
        long sizeKB = chosen.sizeKB;
        sendLine(Protocol.SEND + " " + sizeKB + "\n");

        try(InputStream fis = Files.newInputStream(fpath)){
            byte[] buf = new byte[Config.MAX_CHUNK_SIZE_KB*1024];
            int r;
            DataOutputStream dos = new DataOutputStream(socket.getOutputStream());
            while ((r = fis.read(buf)) != -1) {
                dos.writeInt(r);
                dos.write(buf,0,r);
                dos.flush();
            }
            sendLine(Protocol.DONE + "\n");
            writeHistory(username, filename, "DOWNLOAD", "SUCCESS");
        }catch (Exception e){
            sendLine("ERROR during_send\n");
            writeHistory(username, filename, "DOWNLOAD", "FAILED");
        }
    }

    private void handleRequest(String args)throws IOException{
        String[] toks = args.split(" ", 2);
        if (toks.length < 2) {
            sendLine("ERROR invalid args\n");
            return;
        }
        String recipient = toks[0];
        String desc = toks[1];
        Models.Request req = new Models.Request(username, recipient, desc);
        state.requests.put(req.id, req);
        if(recipient.equalsIgnoreCase("ALL")){
            synchronized (state.knownUsers){
                for(String u: state.knownUsers){
                    if(u.equals(username))continue;
                    Models.Message m = new Models.Message(username, "Request " + req.id + ": " + desc);
                    state.messages.computeIfAbsent(u, k->Collections.synchronizedList(new ArrayList<>())).add(m);
                    ClientHandler ch = state.online.get(u);
                    if (ch!=null) ch.sendLine("REQUEST_FROM " + username + " " + req.id + " " + desc + "\n");

                }
            }
        } else {
            Models.Message m = new Models.Message(username, "Request " + req.id + ": " + desc);
            state.messages.computeIfAbsent(recipient, k->Collections.synchronizedList(new ArrayList<>())).add(m);
            ClientHandler ch = state.online.get(recipient);
            if (ch!=null) ch.sendLine("REQUEST_FROM " + username + " " + req.id + " " + desc + "\n");
        }
        sendLine(Protocol.REQUEST_CREATED + " " + req.id + "\n");
    }

    private void handleMessages() throws IOException{
        List<Models.Message> ms = state.messages.getOrDefault(username, Collections.emptyList());
        StringBuilder sb = new StringBuilder();
        for (Models.Message m: ms) {
            if (!m.read) {
                sb.append(m.fromUser).append(": ").append(m.content).append(" (at ").append(m.at).append(")\n");
                m.read = true;
            }
        }
        sendLine(sb.toString());
    }

    private void handleHistory() throws IOException{
        Path history = Paths.get("data", username, "history.log");
        if (!Files.exists(history)) {
            sendLine("\n");
            return;
        }
        List<String> lines = Files.readAllLines(history);
        StringBuilder sb = new StringBuilder();
        for (String l: lines) sb.append(l).append("\n");
        sendLine(sb.toString());
    }

    public synchronized void sendLine(String s){
        try {
            out.write(s);
            out.flush();
        }catch (IOException e){

        }

    }

    private void logoutCleanup(){
        if(username != null){
            state.online.remove(username);
        }
        closeSilently();
    }

    private void closeSilently() {
        try {
            socket.close();
        } catch (Exception e) {

        }
    }

    private void writeHistory(String user, String filename, String action, String status){
        try{
            Path hist = Paths.get("data", user, "history.log");
            Files.createDirectories(hist.getParent());
            String line = LocalDateTime.now() + "\t" + filename + "\t" + action + "\t" + status + "\n";
            Files.write(hist, line.getBytes(), java.nio.file.StandardOpenOption.CREATE, java.nio.file.StandardOpenOption.APPEND);
        } catch (Exception e){

        }
    }

    private void deleteDirectory(Path dir) {
        try {
            if (Files.exists(dir)) {
                Files.walk(dir).sorted(Comparator.reverseOrder()).map(Path::toFile).forEach(File::delete);
            }
        } catch (IOException e) {

        }
    }
}