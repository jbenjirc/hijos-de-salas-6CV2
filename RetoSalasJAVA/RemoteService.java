import java.rmi.Remote;
import java.rmi.RemoteException;
import java.util.Date;

public interface RemoteService extends Remote {
    // Métodos solicitados
    Date getTime() throws RemoteException;

    String echo(String message) throws RemoteException;

    Object compute(Operation op) throws RemoteException;

    // Método para la analítica de logs
    MetricsReport getMetricsReport() throws RemoteException;
}