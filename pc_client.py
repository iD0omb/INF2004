import socket
import time

# Configuration - Use the IP from your Pico
PICO_IP = "10.222.29.238"  # Your Pico's IP address
UDP_PORT = 1234

# Create UDP socket
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(5.0)

print(f"Sending data to Pico at {PICO_IP}:{UDP_PORT}")

try:
    while True:
        # Send message to Pico
        message = input("Enter message to send (or 'quit' to exit): ")
        
        if message.lower() == 'quit':
            break
        
        sock.sendto(message.encode('utf-8'), (PICO_IP, UDP_PORT))
        print(f"Sent: {message}")
        
        # Wait for response
        try:
            data, addr = sock.recvfrom(1024)
            # Handle potential encoding issues
            try:
                decoded = data.decode('utf-8')
                print(f"Received from Pico: {decoded}")
            except UnicodeDecodeError:
                # Show raw bytes if UTF-8 decoding fails
                print(f"Received from Pico (raw): {data}")
                # Try with error handling
                decoded = data.decode('utf-8', errors='replace')
                print(f"Received from Pico (decoded): {decoded}")
        except socket.timeout:
            print("No response received (timeout)")
        
        time.sleep(0.5)

except KeyboardInterrupt:
    print("\nExiting...")

finally:
    sock.close()
