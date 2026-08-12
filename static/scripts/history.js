const API_URL = `${window.location.protocol}//${window.location.host}`;
let autoRefreshInterval = null;

function loadHistory() {
    fetch(`${API_URL}/history`)
        .then(response => {
            if (!response.ok) {
                throw new Error(`HTTP error! status: ${response.status}`);
            }
            return response.json();
        })
        .then(data => {
            const tbody = document.getElementById('historyTableBody');
            tbody.innerHTML = '';

            if (data.length === 0) {
                tbody.innerHTML = '<tr><td colspan="9">No history records found</td></tr>';
                return;
            }

            data.forEach(record => {
                const row = document.createElement('tr');
                row.innerHTML = `
                    <td>${record.history_id}</td>
                    <td>${formatDateTime(record.created_at)}</td>
                    <td>${record.schedule_id || 'N/A'}</td>
                    <td>${record.module_id || 'N/A'}</td>
                    <td>${formatDate(record.feed_date)}</td>
                    <td>${record.feed_time || 'N/A'}</td>
                    <td>${record.amount ? record.amount + 'g' : 'N/A'}</td>
                    <td><span class="status-badge status-${record.status}">${record.status || 'N/A'}</span></td>
                    <td>
                        <button class="btn-delete" onclick="deleteHistory(${record.history_id})">Delete</button>
                    </td>
                `;
                tbody.appendChild(row);
            });
        })
        .catch(error => {
            console.error('Error loading history:', error);
            document.getElementById('historyTableBody').innerHTML =
                `<tr><td colspan="9">Error loading history: ${error.message}</td></tr>`;
        });
}

function formatDateTime(dateTimeString) {
    if (!dateTimeString) return 'N/A';
    try {
        const date = new Date(dateTimeString);
        return date.toLocaleString('en-US', {
            year: 'numeric',
            month: 'short',
            day: 'numeric',
            hour: '2-digit',
            minute: '2-digit',
            second: '2-digit'
        });
    } catch (e) {
        return dateTimeString;
    }
}

function formatDate(dateString) {
    if (!dateString) return 'N/A';
    try {
        const date = new Date(dateString);
        return date.toLocaleDateString('en-US', {
            year: 'numeric',
            month: 'short',
            day: 'numeric'
        });
    } catch (e) {
        return dateString;
    }
}

function deleteHistory(historyId) {
    if (!confirm('Are you sure you want to delete this history record?')) {
        return;
    }

    if (autoRefreshInterval) {
        clearInterval(autoRefreshInterval);
    }

    fetch(`${API_URL}/history/${historyId}`, {
        method: 'DELETE',
        headers: {
            'Content-Type': 'application/json'
        }
    })
        .then(response => {
            if (!response.ok) {
                throw new Error(`HTTP error! status: ${response.status}`);
            }
            return response.json();
        })
        .then(data => {
            if (data.success) {
                showNotification('History record deleted successfully', 'success');
                setTimeout(() => {
                    loadHistory();
                    startAutoRefresh();
                }, 300);
            } else {
                showNotification('Failed to delete: ' + (data.message || data.error || 'Unknown error'), 'error');
                startAutoRefresh();
            }
        })
        .catch(error => {
            console.error('Error deleting history:', error);
            showNotification('Error deleting history record: ' + error.message, 'error');
            startAutoRefresh();
        });
}

function showNotification(message, type = 'info') {
    const notification = document.createElement('div');
    const bgColor = type === 'success' ? '#4CAF50' : type === 'error' ? '#f44336' : '#2196F3';
    notification.style.cssText = `
        position: fixed;
        top: 20px;
        right: 20px;
        background: ${bgColor};
        color: white;
        padding: 15px 20px;
        border-radius: 5px;
        box-shadow: 0 2px 5px rgba(0,0,0,0.2);
        z-index: 1000;
        animation: slideIn 0.3s ease-out;
    `;
    notification.textContent = message;
    document.body.appendChild(notification);

    setTimeout(() => {
        notification.style.animation = 'slideOut 0.3s ease-out';
        setTimeout(() => notification.remove(), 300);
    }, 3000);
}

function startAutoRefresh() {
    if (autoRefreshInterval) {
        clearInterval(autoRefreshInterval);
    }
    autoRefreshInterval = setInterval(loadHistory, 5000);
}

function printHistoryPDF() {
    const element = document.createElement('div');
    element.style.padding = '20px';
    element.style.backgroundColor = 'white';

    const table = document.querySelector('table').cloneNode(true);

    // Remove Actions column
    const headerRow = table.querySelector('thead tr') || table.querySelector('tr');
    if (headerRow) {
        const actionHeader = Array.from(headerRow.querySelectorAll('th')).find(th =>
            th.textContent.trim().toUpperCase() === 'ACTIONS'
        );
        if (actionHeader) {
            actionHeader.remove();
        }
    }

    table.querySelectorAll('tbody tr, tr').forEach(row => {
        const cells = row.querySelectorAll('td');
        if (cells.length > 0) {
            const lastCell = cells[cells.length - 1];
            if (lastCell.querySelector('button')) {
                lastCell.remove();
            }
        }
    });

    element.innerHTML = `
        <h1 style="color: #ff6b35; text-align: center;">Feeding History Report</h1>
        <p style="text-align: center; color: #666;">Generated on: ${new Date().toLocaleString()}</p>
        <hr style="margin: 20px 0;">
        ${table.outerHTML}
    `;

    html2pdf()
        .set({
            margin: 10,
            filename: `feeding-history-${new Date().toISOString().split('T')[0]}.pdf`,
            image: { type: 'jpeg', quality: 0.98 },
            html2canvas: { scale: 2 },
            jsPDF: { unit: 'mm', format: 'a4', orientation: 'landscape' }
        })
        .from(element)
        .save();
}

// Load history on page load
loadHistory();
startAutoRefresh();