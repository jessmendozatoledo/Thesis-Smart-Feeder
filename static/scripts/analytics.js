// Analytics functionality
let weeklyChart = null;
let statusChart = null;

// Load analytics data
function loadAnalytics() {
    loadSummaryData();
    loadWeeklyChart();
    loadStatusChart();
}

// Load summary cards data
function loadSummaryData() {
    fetch(`${API_URL}/analytics/summary`)
        .then(response => response.json())
        .then(data => {
            document.getElementById('totalFedToday').textContent = data.total_fed_today.toFixed(0) + 'g';
            document.getElementById('activeModules').textContent = `${data.active_modules}/${data.total_modules}`;
        })
        .catch(error => console.error('Error loading summary:', error));
}

// Load weekly feeding chart
function loadWeeklyChart() {
    fetch(`${API_URL}/analytics/weekly`)
        .then(response => response.json())
        .then(data => {
            const ctx = document.getElementById('weeklyChart').getContext('2d');

            if (weeklyChart) {
                weeklyChart.destroy();
            }

            const allDays = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
            const chartData = allDays.map(day => {
                const found = data.find(d => d.day === day);
                return found ? found.amount : 0;
            });

            weeklyChart = new Chart(ctx, {
                type: 'bar',
                data: {
                    labels: allDays,
                    datasets: [{
                        label: 'Amount Fed (g)',
                        data: chartData,
                        backgroundColor: 'rgba(255, 107, 53, 0.7)',
                        borderColor: 'rgba(255, 107, 53, 1)',
                        borderWidth: 1
                    }]
                },
                options: {
                    responsive: true,
                    maintainAspectRatio: true,
                    scales: {
                        y: {
                            beginAtZero: true,
                            ticks: {
                                callback: function (value) {
                                    return value + 'g';
                                }
                            }
                        }
                    },
                    plugins: {
                        legend: {
                            display: false
                        }
                    }
                }
            });
        })
        .catch(error => console.error('Error loading weekly chart:', error));
}

// Load module status chart
function loadStatusChart() {
    fetch(`${API_URL}/analytics/module-status`)
        .then(response => response.json())
        .then(data => {
            const ctx = document.getElementById('statusChart').getContext('2d');

            if (statusChart) {
                statusChart.destroy();
            }

            const labels = data.map(d => d.status.charAt(0).toUpperCase() + d.status.slice(1));
            const counts = data.map(d => d.count);
            const colors = data.map(d => d.status === 'active' ? '#22c55e' : '#ef4444');

            statusChart = new Chart(ctx, {
                type: 'pie',
                data: {
                    labels: labels,
                    datasets: [{
                        data: counts,
                        backgroundColor: colors,
                        borderWidth: 2,
                        borderColor: '#fff'
                    }]
                },
                options: {
                    responsive: true,
                    maintainAspectRatio: true,
                    plugins: {
                        legend: {
                            position: 'bottom'
                        }
                    }
                }
            });
        })
        .catch(error => console.error('Error loading status chart:', error));
}

function printAnalyticsPDF() {
    const totalFed = document.getElementById('totalFedToday').textContent;
    const activeModules = document.getElementById('activeModules').textContent;
    const weeklyChartImg = weeklyChart.toBase64Image();
    const statusChartImg = statusChart.toBase64Image();

    const element = document.createElement('div');
    element.style.width = '210mm';
    element.style.padding = '20px';
    element.style.backgroundColor = 'white';

    element.innerHTML = `
        <h1 style="color:#ff6b35;text-align:center;margin-bottom:10px;">Analytics Report</h1>
        <p style="text-align:center;color:#666;margin-bottom:20px;">${new Date().toLocaleString()}</p>
        <hr style="margin:15px 0;border:1px solid #ddd;">
        <h2 style="color:#333;margin-top:20px;font-size:16px;">Summary</h2>
        <table style="width:100%;border-collapse:collapse;margin-bottom:20px;">
            <tr style="background-color:#f9f9f9;">
                <td style="padding:12px;border:1px solid #ddd;font-weight:bold;">Total Fed Today</td>
                <td style="padding:12px;border:1px solid #ddd;color:#ff6b35;font-size:16px;">${totalFed}</td>
            </tr>
            <tr>
                <td style="padding:12px;border:1px solid #ddd;font-weight:bold;">Active Modules</td>
                <td style="padding:12px;border:1px solid #ddd;color:#ff6b35;font-size:16px;">${activeModules}</td>
            </tr>
        </table>
        <h2 style="color:#333;margin-top:25px;font-size:16px;">Weekly Feeding Overview</h2>
        <img src="${weeklyChartImg}" style="width:100%;max-width:650px;margin:15px 0;">
        <h2 style="color:#333;margin-top:25px;font-size:16px;">Module Status Distribution</h2>
        <img src="${statusChartImg}" style="width:500px;margin:15px 0;">
    `;

    html2pdf()
        .set({
            margin: 10,
            filename: `analytics-report-${new Date().toISOString().split('T')[0]}.pdf`,
            image: { type: 'jpeg', quality: 0.95 },
            html2canvas: { scale: 1.5, logging: false },
            jsPDF: { unit: 'mm', format: 'a4', orientation: 'portrait' }
        })
        .from(element)
        .save();
}

// Load analytics on page load
loadAnalytics();

// Refresh analytics every 10 seconds
setInterval(loadAnalytics, 10000);