import sqlite3
from flask import Flask, render_template, request, jsonify
from datetime import datetime

app = Flask(__name__)
DB_NAME = "sensor_data.db"

# --- 1. DATABASE SETUP ---
def init_db():
    """Creates the local database table if it doesn't exist."""
    with sqlite3.connect(DB_NAME) as conn:
        cursor = conn.cursor()
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS readings (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp TEXT,
                current REAL,
                voltage REAL,   
                power REAL,
                status TEXT
            )
        ''')
        conn.commit()

# Initialize DB immediately on startup
init_db()

# Global variable for "Live" view (Instant access without querying DB)
live_data = {
    "current": 0.0,
    "status": "WAITING FOR DEVICE...",
    "timestamp": "--:--:--"
}

# --- 2. ROUTE: DASHBOARD ---
@app.route('/')
def home():
    return render_template('index.html')

# --- 3. ROUTE: RECEIVE DATA FROM ESP32 (POST) ---
@app.route('/update_sensor', methods=['POST'])
def update_sensor():
    global live_data
    try:
        data = request.get_json()
        
        # A. Extract Data
        current = float(data.get('current', 0.0))
        status = str(data.get('status', 'Unknown'))
        
        # B. Derived Calculations
        voltage = 220.0 # Fixed standard voltage
        power = round(voltage * current, 2) # Watts = V * I
        timestamp_full = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        timestamp_time = datetime.now().strftime("%H:%M:%S")

        # C. Update Live View (Memory)
        live_data = {
            "current": current,
            "status": status,
            "timestamp": timestamp_time
        }

        # D. Save to Database (Disk)
        with sqlite3.connect(DB_NAME) as conn:
            cursor = conn.cursor()
            cursor.execute('''
                INSERT INTO readings (timestamp, current, voltage, power, status)
                VALUES (?, ?, ?, ?, ?)
            ''', (timestamp_full, current, voltage, power, status))
            conn.commit()

        print(f"[{timestamp_time}] Logged: {current}A | {power}W | {status}")
        return jsonify({"message": "Data Logged Successfully"}), 200

    except Exception as e:
        print(f"Error processing data: {e}")
        return jsonify({"message": "Error"}), 400

# --- 4. ROUTE: SEND LIVE DATA TO FRONTEND (GET) ---
@app.route('/get_live_data', methods=['GET'])
def get_live_data():
    return jsonify(live_data)

# --- 5. ROUTE: SEND GRAPH HISTORY TO FRONTEND (GET) ---
@app.route('/get_history', methods=['GET'])
def get_history():
    try:
        # Fetch last 30 readings for the graph
        with sqlite3.connect(DB_NAME) as conn:
            cursor = conn.cursor()
            cursor.execute('SELECT timestamp, power FROM readings ORDER BY id DESC LIMIT 30')
            rows = cursor.fetchall()
            
        # Reverse rows so the graph flows Left -> Right (Oldest to Newest)
        rows.reverse()
        
        data = {
            "labels": [row[0].split(" ")[1] for row in rows], # Extract time part only
            "power": [row[1] for row in rows]
        }
        return jsonify(data)
    except Exception as e:
        return jsonify({"error": str(e)})

if __name__ == '__main__':
    print("------------------------------------------------")
    print(" SafeSocket Server Started")
    print(" Database: Initialized")
    print(" Waiting for ESP32 connection...")
    print("------------------------------------------------")
    # host='0.0.0.0' allows external devices (ESP32) to connect
    app.run(host='0.0.0.0', port=5000, debug=True)