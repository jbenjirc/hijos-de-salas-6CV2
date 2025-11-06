import java.rmi.RemoteException;
import java.rmi.registry.LocateRegistry;
import java.rmi.registry.Registry;
import java.rmi.server.UnicastRemoteObject;
import java.util.Date;

public class RemoteServiceImpl extends UnicastRemoteObject implements RemoteService {

    // Objeto para almacenar las métricas de las llamadas (Analítica)
    private final MetricsReport metrics = new MetricsReport();

    // Constructor que lanza RemoteException, necesario al extender
    // UnicastRemoteObject
    protected RemoteServiceImpl() throws RemoteException {
        super(0); // El '0' indica que el puerto exportado será anónimo/dinámico
        System.out.println("Servicio RMI inicializado en puerto exportado dinámico.");
    }

    // Implementación de getTime()
    @Override
    public Date getTime() throws RemoteException {
        metrics.incrementTime();
        System.out.println("-> Invocación getTime() recibida.");
        return new Date();
    }

    // Implementación de echo(String)
    @Override
    public String echo(String message) throws RemoteException {
        metrics.incrementEcho();
        System.out.println("-> Invocación echo() recibida. Mensaje: " + message);
        return "Servidor dice: " + message;
    }

    // Implementación de compute(Operation)
    @Override
    public Object compute(Operation op) throws RemoteException {
        metrics.incrementCompute();
        System.out.println("-> Invocación compute() recibida. Operación: " + op);

        switch (op.getType()) {
            case SUM:
                return op.getOperand1() + op.getOperand2();
            case SUBTRACT:
                return op.getOperand1() - op.getOperand2();
            default:
                throw new RemoteException("Operación no soportada.");
        }
    }

    // Implementación de getMetricsReport()
    @Override
    public MetricsReport getMetricsReport() throws RemoteException {
        System.out.println("-> Invocación getMetricsReport() recibida.");
        return metrics;
    }

    public static void main(String[] args) {
        final int RMI_PORT = 1099;
        final String SERVICE_NAME = "RemoteService";

        try {
            // Crear el objeto remoto
            RemoteService service = new RemoteServiceImpl();

            // Crear y obtener el Registro RMI en el puerto 1099
            Registry registry = LocateRegistry.createRegistry(RMI_PORT);

            // Registrar el objeto remoto (su stub)
            registry.rebind(SERVICE_NAME, service);

            System.out.println("Servidor RMI listo. Nombre: " + SERVICE_NAME + ", Puerto Registro: " + RMI_PORT);
            System.out.println("Esperando invocaciones de clientes...");

        } catch (Exception e) {
            System.err.println("Excepción en el servidor: " + e.toString());
            e.printStackTrace();
        }
    }
}