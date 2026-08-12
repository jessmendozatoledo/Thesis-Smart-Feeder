const API_URL = '/schedules';
const MODULES_API_URL = '/modules';

// Load modules for dropdown
async function loadModulesForDropdown() {
    try {
        const res = await fetch(MODULES_API_URL);
        const modules = await res.json();
        const moduleIdInput = document.getElementById('module_id');
        moduleIdInput.innerHTML = '';
        modules.forEach(module => {
            const option = document.createElement('option');
            option.value = module.module_id;
            option.textContent = module.module_id;
            moduleIdInput.appendChild(option);
        });
    } catch (error) {
        console.error('Error loading modules:', error);
    }
}

async function addSchedule() {
    const module_id = document.getElementById('module_id').value;
    const feed_date = document.getElementById('feed_date').value;
    const feed_time = document.getElementById('feed_time').value;
    const amount = document.getElementById('amount').value;

    if (!module_id || !feed_date || !feed_time || !amount) {
        alert('Please fill in all fields');
        return;
    }

    try {
        const res = await fetch(API_URL, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                module_id,
                feed_date,
                feed_time,
                amount: parseFloat(amount),
                status: 'pending'
            })
        });
        const result = await res.json();
        alert(result.success ? 'Schedule added!' : 'Error adding schedule');
        loadSchedules();
        document.getElementById('addScheduleForm').reset();
    } catch (error) {
        console.error('Error adding schedule:', error);
        alert('Error adding schedule');
    }
}

async function addRecurringSchedule() {
    const module_id = document.getElementById('module_id').value;
    const feed_time = document.getElementById('feed_time').value;
    const amount = document.getElementById('amount').value;
    const start_date = document.getElementById('feed_date').value;

    if (!module_id || !feed_time || !amount || !start_date) {
        alert('Please fill in all fields');
        return;
    }

    try {
        const res = await fetch(`${API_URL}/recurring`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                module_id,
                feed_time,
                amount: parseFloat(amount),
                start_date,
                days_ahead: 7
            })
        });
        const result = await res.json();
        if (result.success) {
            alert(`Added ${result.created_count} recurring schedules for ${result.dates.length} days`);
            loadSchedules();
        } else {
            alert('Error adding recurring schedules');
        }
    } catch (error) {
        console.error('Error adding recurring schedule:', error);
        alert('Error adding recurring schedule');
    }
}

async function loadSchedules() {
    try {
        const res = await fetch(API_URL);
        const data = await res.json();
        const tbody = document.getElementById('scheduleTableBody');
        tbody.innerHTML = '';

        if (data.length === 0) {
            tbody.innerHTML = '<tr><td colspan="7">No schedules found</td></tr>';
            return;
        }

        data.forEach(schedule => {
            const row = document.createElement('tr');
            row.innerHTML = `
                <td>${schedule.schedule_id}</td>
                <td>${schedule.module_id}</td>
                <td>${schedule.feed_date}</td>
                <td>${schedule.feed_time}</td>
                <td>${schedule.amount}g</td>
                <td><span class="status-badge status-${schedule.status}">${schedule.status}</span></td>
                <td>
                    <button onclick="deleteSchedule(${schedule.schedule_id})" class="btn-delete">Delete</button>
                </td>
            `;
            tbody.appendChild(row);
        });
    } catch (error) {
        console.error('Error loading schedules:', error);
        document.getElementById('scheduleTableBody').innerHTML =
            '<tr><td colspan="7">Error loading schedules</td></tr>';
    }
}

async function deleteSchedule(schedule_id) {
    if (!confirm(`Are you sure you want to delete schedule ${schedule_id}?`)) {
        return;
    }

    try {
        const res = await fetch(`${API_URL}/${schedule_id}`, {
            method: 'DELETE'
        });
        const result = await res.json();
        alert(result.success ? 'Schedule deleted!' : 'Error deleting schedule');
        loadSchedules();
    } catch (error) {
        console.error('Error deleting schedule:', error);
        alert('Error deleting schedule');
    }
}

// Initialize
loadModulesForDropdown();
loadSchedules();