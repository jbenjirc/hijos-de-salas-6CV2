import java.io.Serializable;

public class Operation implements Serializable {
    private static final long serialVersionUID = 1L;

    public enum Type {
        SUM, SUBTRACT
    }

    private Type type;
    private double operand1;
    private double operand2;

    public Operation(Type type, double operand1, double operand2) {
        this.type = type;
        this.operand1 = operand1;
        this.operand2 = operand2;
    }

    public Type getType() {
        return type;
    }

    public double getOperand1() {
        return operand1;
    }

    public double getOperand2() {
        return operand2;
    }

    @Override
    public String toString() {
        return type.name() + "(" + operand1 + ", " + operand2 + ")";
    }
}