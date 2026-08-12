# app.py
from flask import Flask, jsonify, request, render_template, send_from_directory
from flask_cors import CORS
import sqlite3
import os
import time
import threading
from datetime import datetime, timedelta
import pytz
from dotenv import load_dotenv

# Load environment variables from .env file
load_dotenv()

# Config environment variables
HOST = os.getenv("HOST", "0.0.0.0")
PORT = int(os.getenv("PORT", 8080))
DEBUG = os.getenv("FLASK_DEBUG", "True").lower() in ("true", "1", "t")
TIMEZONE_NAME = os.getenv("TIMEZONE", "Asia/Manila")
DB_NAME = os.getenv("DB_NAME", "animal_feeder.db")

# Add Philippine timezone
PH_TZ = pytz.timezone(TIMEZONE_NAME)

# - - - - - - - - - - - - - - - - - - - - App setup
app = Flask(__name__, instance_relative_config=True)
CORS(app)

DB_PATH = os.path.join(app.instance_path, DB_NAME)

# - - - - - - - - - - - - - - - - - - - - Database helper
def query_db(query, args=(), one=False):
    con = None
    try:
        con = sqlite3.connect(DB_PATH, timeout=30, check_same_thread=False)
        con.row_factory = sqlite3.Row
        cur = con.cursor()
        cur.execute(query, args)
        rv = cur.fetchall()
        con.commit()
        return (rv[0] if rv else None) if one else rv
    except sqlite3.OperationalError as e:
        if con:
            con.rollback()
        raise e
    finally:
        if con:
            con.close()

os.makedirs(app.instance_path, exist_ok=True)

# -- Table creation
query_db("""
CREATE TABLE IF NOT EXISTS camera (
    cam_id TEXT PRIMARY KEY,
    status TEXT NOT NULL
)
""")

query_db("""
CREATE TABLE IF NOT EXISTS modules (
    module_id TEXT PRIMARY KEY,
    cam_id TEXT NOT NULL,
    status TEXT NOT NULL,
    weight REAL,
    FOREIGN KEY (cam_id) REFERENCES camera(cam_id)
)
""")

# Add migration for schedules table
try:
    query_db("""
    CREATE TABLE IF NOT EXISTS schedules (
        schedule_id INTEGER PRIMARY KEY AUTOINCREMENT,
        module_id TEXT NOT NULL,
        feed_date DATE NOT NULL,
        feed_time TEXT NOT NULL,
        amount REAL NOT NULL,
        status TEXT DEFAULT 'pending' CHECK(status IN ('pending', 'done', 'cancelled')),
        FOREIGN KEY (module_id) REFERENCES modules(module_id)
    )
    """)
    print("Created new schedules table with feed_date column")
except Exception as e:
    print(f"Error during migration: {e}")

query_db("""
CREATE TABLE IF NOT EXISTS history (
    history_id INTEGER PRIMARY KEY AUTOINCREMENT,
    schedule_id INTEGER,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (schedule_id) REFERENCES schedules(schedule_id)
)
""")

query_db("""
CREATE TABLE IF NOT EXISTS image_metadata (
    filename TEXT PRIMARY KEY,
    camera_id TEXT NOT NULL,
    timestamp INTEGER NOT NULL,
    category TEXT NOT NULL CHECK(category IN ('during', 'after')),
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
)
""")

# ============================================================
# ESP32/DEVICE ROUTES
# ============================================================

@app.route("/health")
def health_check():
    """mDNS/health check endpoint for devices"""
    return "mDNS OK"

@app.route("/check_schedule", methods=["POST"])
def check_schedule():
    """Check if a module should dispense food now"""
    module_id = request.form.get("module_id")
    
    if not module_id:
        return jsonify({"error": "Missing module_id"}), 400
    
    module = query_db("SELECT module_id FROM modules WHERE module_id=? AND status='active'", (module_id,), one=True)
    if not module:
        return jsonify({"error": "Invalid or inactive module_id"}), 404
    
    now = datetime.now()
    current_date = now.strftime("%Y-%m-%d")
    current_time = now.strftime("%H:%M")
    
    row = query_db("""
        SELECT schedule_id, amount, feed_time, feed_date 
        FROM schedules 
        WHERE module_id=? AND feed_date=? AND feed_time<=? AND status='pending' 
        ORDER BY feed_time ASC LIMIT 1
    """, (module_id, current_date, current_time), one=True)
    
    if row:
        return jsonify({
            "dispense": True,
            "amount": row['amount'],
            "schedule_id": row['schedule_id'],
            "scheduled_date": row['feed_date'],
            "scheduled_time": row['feed_time']
        })
    else:
        return jsonify({"dispense": False})

@app.route("/complete_schedule", methods=["POST"])
def complete_schedule():
    """Mark a schedule as done and add to history"""
    schedule_id = request.form.get("schedule_id")
    module_id = request.form.get("module_id")
    
    if not schedule_id:
        return jsonify({"error": "Missing schedule_id"}), 400
    
    schedule = query_db("SELECT schedule_id, module_id, status FROM schedules WHERE schedule_id=?", (schedule_id,), one=True)
    
    if not schedule:
        return jsonify({"error": "Schedule not found"}), 404
    
    if schedule['status'] == 'done':
        return jsonify({"error": "Schedule already completed"}), 400
    
    if module_id and schedule['module_id'] != module_id:
        return jsonify({"error": "Module ID mismatch"}), 403
    
    query_db("UPDATE schedules SET status='done' WHERE schedule_id=?", (schedule_id,))
    query_db("INSERT INTO history (schedule_id) VALUES (?)", (schedule_id,))
    
    print(f"Schedule {schedule_id} completed by module {schedule['module_id']}")
    
    return jsonify({
        "success": True,
        "message": "Schedule completed successfully",
        "schedule_id": schedule_id
    })

@app.route("/weight_update", methods=["POST"])
def weight_update():
    """Update module weight from ESP32"""
    module_id = request.form.get("module_id")
    weight = request.form.get("weight")
    
    if not module_id or weight is None:
        return jsonify({"error": "Missing module_id or weight"}), 400
    
    try:
        weight_value = float(weight)
        if weight_value < 0 or weight_value > 10000:
            return jsonify({"error": "Invalid weight value"}), 400
    except ValueError:
        return jsonify({"error": "Weight must be a number"}), 400
    
    print(f"Weight update - Device: {module_id}, Weight: {weight_value}g")
    
    existing = query_db("SELECT module_id, status FROM modules WHERE module_id=?", (module_id,), one=True)
    
    if existing:
        query_db("UPDATE modules SET weight=? WHERE module_id=?", (weight_value, module_id))
        return jsonify({
            "success": True,
            "message": f"Weight updated for {module_id}: {weight_value}g",
            "current_status": existing['status']
        })
    else:
        return jsonify({"error": "Module not registered. Please register module first."}), 403

@app.route('/api/snapshots/<filename>', methods=['DELETE'])
def delete_snapshot(filename):
    image_dir = 'instance/images'
    try:
        filepath = os.path.join(image_dir, filename)
        
        if not os.path.exists(filepath):
            return jsonify({'success': False, 'error': 'File not found'}), 404
        
        if '..' in filename or '/' in filename or '\\' in filename:
            return jsonify({'success': False, 'error': 'Invalid filename'}), 400
        
        os.remove(filepath)
        query_db("DELETE FROM image_metadata WHERE filename = ?", (filename,))
        
        print(f"Deleted image: {filename}")
        
        return jsonify({'success': True, 'message': f'Image {filename} deleted successfully'})
    except Exception as e:
        print(f"Error deleting image {filename}: {str(e)}")
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route("/upload_image", methods=["POST"])
def upload_image():
    """Receive image from ESP32-CAM"""
    camera_id = request.form.get("camera_id")
    category = request.form.get("category", "during")
    
    if not camera_id:
        return jsonify({"error": "Missing camera_id"}), 400
    
    camera = query_db("SELECT cam_id FROM camera WHERE cam_id=? AND status='active'", (camera_id,), one=True)
    if not camera:
        return jsonify({"error": "Invalid or inactive camera_id"}), 404
    
    image = request.files.get('image')
    if not image:
        return jsonify({"error": "No image data"}), 400
    
    images_dir = os.path.join(app.instance_path, 'images')
    os.makedirs(images_dir, exist_ok=True)
    
    timestamp = int(time.time())
    filename = f"{camera_id}_{timestamp}.jpg"
    filepath = os.path.join(images_dir, filename)
    image.save(filepath)
    file_size = os.path.getsize(filepath)
    
    query_db("INSERT OR REPLACE INTO image_metadata (filename, camera_id, timestamp, category) VALUES (?, ?, ?, ?)",
             (filename, camera_id, timestamp, category))
    
    print(f"Saved: {filename}, Size: {file_size} bytes, Camera: {camera_id}, Category: {category}")
    
    return jsonify({
        "success": True,
        "filename": filename,
        "size": file_size,
        "camera_id": camera_id,
        "category": category
    }), 200

# ============================================================
# CAMERA ROUTES
# ============================================================

@app.route("/cameras", methods=["GET"])
def get_cameras():
    rows = query_db("SELECT * FROM camera")
    return jsonify([dict(row) for row in rows])

@app.route("/cameras", methods=["POST"])
def add_camera():
    data = request.get_json()
    query_db("INSERT INTO camera (cam_id, status) VALUES (?, ?)", (data["cam_id"], data["status"]))
    return jsonify({"success": True})

@app.route("/cameras/<cam_id>", methods=["PUT"])
def update_camera(cam_id):
    data = request.get_json()
    query_db("UPDATE camera SET status = ? WHERE cam_id = ?", (data["status"], cam_id))
    return jsonify({"success": True})

@app.route("/cameras/<cam_id>", methods=["DELETE"])
def delete_camera(cam_id):
    query_db("DELETE FROM camera WHERE cam_id = ?", (cam_id,))
    return jsonify({"success": True})

@app.route('/api/snapshots', methods=['GET'])
def get_snapshots():
    try:
        rows = query_db("""
            SELECT filename, camera_id, timestamp, category 
            FROM image_metadata 
            ORDER BY timestamp DESC
        """)
        
        images_with_metadata = []
        for row in rows:
            images_with_metadata.append({
                'filename': row['filename'],
                'camera_id': row['camera_id'],
                'timestamp': row['timestamp'],
                'category': row['category']
            })
        
        return jsonify({
            'success': True,
            'images': images_with_metadata
        })
    except Exception as e:
        print(f"Error loading snapshots: {str(e)}")
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/snapshots/<filename>')
def serve_snapshot(filename):
    try:
        return send_from_directory('instance/images', filename)
    except Exception as e:
        return jsonify({'success': False, 'error': str(e)}), 404

@app.route('/api/snapshots/<cam_id>', methods=['GET'])
def get_camera_snapshots(cam_id):
    try:
        rows = query_db("""
            SELECT filename, camera_id, timestamp, category 
            FROM image_metadata 
            WHERE camera_id = ? 
            ORDER BY timestamp DESC
        """, (cam_id,))
        
        camera_images = []
        for row in rows:
            camera_images.append({
                'filename': row['filename'],
                'camera_id': row['camera_id'],
                'timestamp': row['timestamp'],
                'category': row['category']
            })
        
        return jsonify({
            'success': True,
            'cam_id': cam_id,
            'images': camera_images
        })
    except Exception as e:
        return jsonify({'success': False, 'error': str(e)}), 500

# ============================================================
# MODULE ROUTES
# ============================================================

@app.route("/modules", methods=["GET"])
def get_modules():
    rows = query_db("SELECT * FROM modules")
    return jsonify([dict(row) for row in rows])

@app.route("/modules", methods=["POST"])
def add_module():
    data = request.get_json()
    query_db("INSERT INTO modules (module_id, cam_id, status, weight) VALUES (?, ?, ?, ?)",
             (data["module_id"], data["cam_id"], data["status"], data["weight"]))
    return jsonify({"success": True})

@app.route("/modules/<module_id>", methods=["PUT"])
def update_module(module_id):
    data = request.get_json()
    query_db("UPDATE modules SET cam_id = ?, status = ?, weight = ? WHERE module_id = ?",
             (data["cam_id"], data["status"], data["weight"], module_id))
    return jsonify({"success": True})

@app.route("/modules/<module_id>", methods=["DELETE"])
def delete_module(module_id):
    query_db("DELETE FROM modules WHERE module_id = ?", (module_id,))
    return jsonify({"success": True})

# ============================================================
# SCHEDULE ROUTES
# ============================================================

@app.route("/schedules", methods=["GET"])
def get_schedules():
    start_date = request.args.get('start_date')
    end_date = request.args.get('end_date')
    module_id = request.args.get('module_id')
    
    query = "SELECT * FROM schedules WHERE 1=1"
    params = []
    
    if module_id:
        query += " AND module_id = ?"
        params.append(module_id)
    
    if start_date:
        query += " AND feed_date >= ?"
        params.append(start_date)
    
    if end_date:
        query += " AND feed_date <= ?"
        params.append(end_date)
    
    query += " ORDER BY feed_date, feed_time"
    rows = query_db(query, tuple(params))
    return jsonify([dict(row) for row in rows])

@app.route("/schedules", methods=["POST"])
def add_schedule():
    data = request.get_json()
    if not data.get("feed_date"):
        return jsonify({"error": "feed_date is required"}), 400
    
    query_db("INSERT INTO schedules (module_id, feed_date, feed_time, amount, status) VALUES (?, ?, ?, ?, ?)",
             (data["module_id"], data["feed_date"], data["feed_time"], data["amount"], data.get("status", "pending")))
    return jsonify({"success": True})

@app.route("/schedules/recurring", methods=["POST"])
def add_recurring_schedule():
    """Add a recurring schedule for multiple days"""
    data = request.get_json()
    
    if not data.get("feed_time"):
        return jsonify({"error": "feed_time is required"}), 400
    if not data.get("amount"):
        return jsonify({"error": "amount is required"}), 400
    if not data.get("start_date"):
        return jsonify({"error": "start_date is required"}), 400
    
    module_id = data["module_id"]
    feed_time = data["feed_time"]
    amount = data["amount"]
    days_ahead = data.get("days_ahead", 7)
    
    try:
        start_date = datetime.strptime(data["start_date"], "%Y-%m-%d").date()
    except ValueError:
        return jsonify({"error": "Invalid start_date format. Use YYYY-MM-DD"}), 400
    
    created_schedules = []
    
    for day_offset in range(days_ahead):
        schedule_date = start_date + timedelta(days=day_offset)
        feed_date = schedule_date.strftime("%Y-%m-%d")
        
        existing = query_db("SELECT schedule_id FROM schedules WHERE module_id=? AND feed_date=? AND feed_time=?",
                           (module_id, feed_date, feed_time), one=True)
        
        if not existing:
            query_db("INSERT INTO schedules (module_id, feed_date, feed_time, amount, status) VALUES (?, ?, ?, ?, 'pending')",
                    (module_id, feed_date, feed_time, amount))
            created_schedules.append(feed_date)
    
    return jsonify({
        "success": True,
        "created_count": len(created_schedules),
        "dates": created_schedules
    })

@app.route("/schedules/<int:schedule_id>", methods=["PUT"])
def update_schedule(schedule_id):
    data = request.get_json()
    query_db("UPDATE schedules SET module_id = ?, feed_date = ?, feed_time = ?, amount = ?, status = ? WHERE schedule_id = ?",
             (data["module_id"], data["feed_date"], data["feed_time"], data["amount"], data["status"], schedule_id))
    return jsonify({"success": True})

@app.route("/schedules/<int:schedule_id>", methods=["DELETE"])
def delete_schedule(schedule_id):
    query_db("DELETE FROM schedules WHERE schedule_id = ?", (schedule_id,))
    return jsonify({"success": True})

# ============================================================
# HISTORY ROUTES
# ============================================================

@app.route("/history", methods=["GET"])
def get_history():
    rows = query_db("""
        SELECT h.history_id, h.created_at, s.schedule_id, s.module_id, s.feed_date, s.feed_time, s.amount, s.status
        FROM history h
        LEFT JOIN schedules s ON h.schedule_id = s.schedule_id
        ORDER BY h.created_at DESC
    """)
    
    result = []
    for row in rows:
        row_dict = dict(row)
        if row_dict['created_at']:
            utc_time = datetime.strptime(row_dict['created_at'], '%Y-%m-%d %H:%M:%S')
            utc_time = pytz.utc.localize(utc_time)
            ph_time = utc_time.astimezone(PH_TZ)
            row_dict['created_at'] = ph_time.strftime('%Y-%m-%d %H:%M:%S')
        result.append(row_dict)
    return jsonify(result)

@app.route("/history", methods=["POST"])
def add_history():
    data = request.get_json()
    query_db("INSERT INTO history (schedule_id) VALUES (?)", (data["schedule_id"],))
    return jsonify({"success": True})

@app.route("/history/<int:history_id>", methods=["DELETE"])
def delete_history(history_id):
    query_db("DELETE FROM history WHERE history_id = ?", (history_id,))
    return jsonify({"success": True})

# ============================================================
# ANALYTICS ROUTES
# ============================================================

@app.route("/analytics/summary", methods=["GET"])
def get_analytics_summary():
    """Get summary statistics for analytics dashboard"""
    today = datetime.now().strftime("%Y-%m-%d")
    
    total_fed = query_db("""
        SELECT COALESCE(SUM(s.amount), 0) as total 
        FROM history h 
        JOIN schedules s ON h.schedule_id = s.schedule_id 
        WHERE DATE(h.created_at) = ?
    """, (today,), one=True)
    
    active_modules = query_db("SELECT COUNT(*) as count FROM modules WHERE status='active'", one=True)
    total_modules = query_db("SELECT COUNT(*) as count FROM modules", one=True)
    
    return jsonify({
        "total_fed_today": float(total_fed['total']) if total_fed else 0,
        "active_modules": active_modules['count'] if active_modules else 0,
        "total_modules": total_modules['count'] if total_modules else 0
    })

@app.route("/analytics/weekly", methods=["GET"])
def get_weekly_feeding():
    """Get weekly feeding data for chart"""
    rows = query_db("""
        SELECT 
            CASE CAST(strftime('%w', h.created_at) AS INTEGER)
                WHEN 0 THEN 'Sun'
                WHEN 1 THEN 'Mon'
                WHEN 2 THEN 'Tue'
                WHEN 3 THEN 'Wed'
                WHEN 4 THEN 'Thu'
                WHEN 5 THEN 'Fri'
                WHEN 6 THEN 'Sat'
            END as day,
            COALESCE(SUM(s.amount), 0) as amount
        FROM history h
        JOIN schedules s ON h.schedule_id = s.schedule_id
        WHERE h.created_at >= date('now', '-7 days')
        GROUP BY strftime('%w', h.created_at)
        ORDER BY strftime('%w', h.created_at)
    """)
    return jsonify([dict(row) for row in rows])

@app.route("/analytics/module-status", methods=["GET"])
def get_module_status():
    """Get module status distribution"""
    rows = query_db("SELECT status, COUNT(*) as count FROM modules GROUP BY status")
    return jsonify([dict(row) for row in rows])

# ============================================================
# PAGE ROUTES
# ============================================================

@app.route("/")
def serve_index():
    return render_template("index.html")

@app.route("/module.html")
def serve_module():
    return render_template("module.html")

@app.route("/schedule.html")
def serve_schedule():
    return render_template("schedule.html")

@app.route("/history.html")
def serve_history():
    return render_template("history.html")

@app.route("/feeders.html")
def serve_feeders():
    return render_template("feeders.html")

@app.route("/camera.html")
def serve_camera():
    return render_template("camera.html")

# ============================================================
# Run App
# ============================================================

if __name__ == "__main__":
    app.run(host=HOST, port=PORT, debug=DEBUG, threaded=True)