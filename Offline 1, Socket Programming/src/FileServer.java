import java.io.IOException;
import java.net.ServerSocket;
import java.net.Socket;

public class FileServer {
    private final int port;
    private final ServerState state = new ServerState();

    public FileServer(int port) {

        this.port = port;
    }

    public void start() throws IOException {
        try (ServerSocket ss = new ServerSocket(port)) {
            System.out.println("Server started on port " + port);
            while (true) {
                Socket s = ss.accept();
                ClientHandler ch = new ClientHandler(s, state);
                ch.start();
            }
        }
    }

    public static void main(String[] args) throws IOException {
        int port = 6000;
        if (args.length>0) port = Integer.parseInt(args[0]);
        FileServer server = new FileServer(port);
        server.start();
    }
}