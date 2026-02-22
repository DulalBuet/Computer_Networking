import java.io.*;
import java.net.Socket;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Scanner;

public class FileClient {
    private final String host;
    private final int port;
    private Socket socket;
    private BufferedReader in;
    private BufferedWriter out;
    private DataInputStream dis;
    private DataOutputStream dos;
    private String username;

    public FileClient(String host, int port) {
        this.host = host;
        this.port = port;
    }

    public void start() throws IOException {
        socket = new Socket(host, port);
        in = new BufferedReader(new InputStreamReader(socket.getInputStream()));
        out = new BufferedWriter(new OutputStreamWriter(socket.getOutputStream()));
        dis = new DataInputStream(socket.getInputStream());
        dos = new DataOutputStream(socket.getOutputStream());

        Scanner sc = new Scanner(System.in);
        System.out.print("Username: "); username = sc.nextLine();
        ClientUtils.sendLine(out, Protocol.LOGIN + " " + username);
        String resp = in.readLine();
        System.out.println(resp);
        if (!resp.startsWith(Protocol.LOGIN_OK)) { socket.close(); return; }

        while (true) {
            System.out.println("Commands: LIST_USERS | LIST_FILES [MINE|PUBLIC|username] | UPLOAD filepath [public|private] [requestID|-] | DOWNLOAD owner filename | REQUEST recipient description | MESSAGES | HISTORY | LOGOUT");
            System.out.print("cmd> ");
            String line = sc.nextLine();
            if (line.trim().isEmpty()) continue;
            String[] toks = line.split(" ", 2);
            String cmd = toks[0];
            String args = toks.length>1? toks[1] : "";
            if (cmd.equalsIgnoreCase("UPLOAD")) {
                handleUpload(args);
            } else if (cmd.equalsIgnoreCase("DOWNLOAD")) {
                handleDownload(args);
            } else {
                ClientUtils.sendLine(out, cmd + (args.isEmpty()?"":" "+args));

                try { Thread.sleep(150); } catch (InterruptedException e) {}
                    while (in.ready()) {
                        String r = in.readLine();
                        if (r==null) break;
                        System.out.println(r);
                    }
            }
            if (cmd.equalsIgnoreCase("LOGOUT")) break;
        }
        socket.close();
    }

    private void handleUpload(String args) throws IOException {
        String[] toks = args.split(" ");
        if (toks.length < 1) {
            System.out.println("invalid args");
            return;
        }

        String path = toks[0];
        String access = (toks.length>=2? toks[1] : "private");
        String req = (toks.length>=3? toks[2] : "-");
        java.io.File f = new java.io.File(path);
        if (!f.exists()) {
            System.out.println("file not found"); return;
        }

        long sizeKB = (f.length()+1023)/1024;
        ClientUtils.sendLine(out, Protocol.UPLOAD + " " + f.getName() + " " + sizeKB + " " + access + " " + req);
        String resp = in.readLine();
        if (resp==null) {
            System.out.println("no response");
            return;
        }

        if (resp.startsWith(Protocol.UPLOAD_CHUNK_INFO)) {
            String[] p = resp.split(" ");
            String fileID = p[1];
            int chunkKB = Integer.parseInt(p[2]);
            int chunkBytes = chunkKB*1024;

            try (InputStream fis = new FileInputStream(f)) {
                byte[] buf = new byte[chunkBytes];
                int r;
                while ((r = fis.read(buf)) != -1) {
                    ClientUtils.sendLine(out, "CHUNK " + r);
                    dos.write(buf,0,r); dos.flush();
                    String ack = in.readLine();
                    if (ack==null || !ack.startsWith(Protocol.ACK)) { System.out.println("no ack"); return; }
                }

                ClientUtils.sendLine(out, Protocol.COMPLETE);
                String finalResp = in.readLine();
                System.out.println(finalResp);
            }
        } else {
            System.out.println(resp);
        }
    }



    private void handleDownload(String args) throws IOException {
        ClientUtils.sendLine(out, Protocol.DOWNLOAD + " " + args);
        String resp = in.readLine();
        if (resp == null) return;

        if (resp.startsWith(Protocol.SEND)) {
            String[] p = resp.split(" ");
            long sizeKB = Long.parseLong(p[1]);
            System.out.println("Starting download ("+sizeKB+" KB). Saving to downloads/ folder.");
            Files.createDirectories(Paths.get("downloads"));
            String owner = args.split(" ")[0];
            String filename = args.split(" ")[1];
            Path out = Paths.get("downloads", owner + "__" + filename);
            try (OutputStream fos = Files.newOutputStream(out)) {
                while (true) {
                    int len = dis.readInt();
                    if (len<=0) break;
                    byte[] b = new byte[len];
                    dis.readFully(b);
                    fos.write(b);
                    if (in.ready()) {
                        String maybeDone = in.readLine();
                        if (maybeDone!=null && maybeDone.startsWith(Protocol.DONE)) break;
                    }
                }
            }
            System.out.println("Download finished: " + out.toString());
        } else {
            System.out.println(resp);
        }
    }

    public static void main(String[] args) throws IOException {
        if (args.length < 2) { System.out.println("Usage: FileClient <host> <port>"); return; }
        String host = args[0]; int port = Integer.parseInt(args[1]);
        FileClient client = new FileClient(host, port);
        client.start();
    }
}