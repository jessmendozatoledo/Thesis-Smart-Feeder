document.addEventListener('DOMContentLoaded', function () {
    loadSnapshots();
});

function loadSnapshots() {
    const duringGallery = document.getElementById('duringFeedingGallery');
    const afterGallery = document.getElementById('afterFeedingGallery');

    duringGallery.innerHTML = '<div class="loading">Loading images...</div>';
    afterGallery.innerHTML = '<div class="loading">Loading images...</div>';

    fetch('/api/snapshots')
        .then(response => response.json())
        .then(data => {
            if (data.success && data.images && data.images.length > 0) {
                displayCategorizedImages(data.images);
            } else {
                showEmptyState(duringGallery, 'during');
                showEmptyState(afterGallery, 'after');
            }
        })
        .catch(error => {
            console.error('Error loading images:', error);
            duringGallery.innerHTML = '<div class="empty-state"><p>✗ Error loading images</p></div>';
            afterGallery.innerHTML = '<div class="empty-state"><p>✗ Error loading images</p></div>';
        });
}

function displayCategorizedImages(images) {
    const duringGallery = document.getElementById('duringFeedingGallery');
    const afterGallery = document.getElementById('afterFeedingGallery');

    const duringImages = images.filter(img => img.category === 'during');
    const afterImages = images.filter(img => img.category === 'after');

    if (duringImages.length > 0) {
        duringGallery.innerHTML = '';
        duringImages.forEach(imageData => {
            duringGallery.appendChild(createGalleryItem(imageData));
        });
    } else {
        showEmptyState(duringGallery, 'during');
    }

    if (afterImages.length > 0) {
        afterGallery.innerHTML = '';
        afterImages.forEach(imageData => {
            afterGallery.appendChild(createGalleryItem(imageData));
        });
    } else {
        showEmptyState(afterGallery, 'after');
    }
}

function showEmptyState(gallery, type) {
    const message = type === 'during'
        ? '📷 No "During Feeding" images captured yet'
        : '📷 No "After Feeding" images captured yet';

    gallery.innerHTML = `
        <div class="empty-state">
            <p>${message}</p>
            <p style="font-size: 14px; color: #ffaa6e;">Images will appear here after feeding events</p>
        </div>
    `;
}

function createGalleryItem(imageData) {
    const div = document.createElement('div');
    div.className = 'gallery-item';

    const filename = imageData.filename;
    const cameraId = imageData.camera_id || 'Unknown';
    const timestamp = imageData.timestamp || 'Unknown';
    const category = imageData.category || 'unknown';

    div.innerHTML = `
        <img src="/snapshots/${filename}" alt="${filename}" onclick="openModal('${filename}'); event.stopPropagation();">
        <div class="info">
            <p class="timestamp">${formatTimestamp(timestamp)}</p>
            <p>Camera: ${cameraId}</p>
            <p style="font-size: 11px; color: #999;">Category: ${category}</p>
            <p style="font-size: 12px; color: #999;">${filename}</p>
            <button class="delete-btn" onclick="deleteImage('${filename}'); event.stopPropagation();">
                Delete
            </button>
        </div>
    `;

    return div;
}

function formatTimestamp(timestamp) {
    if (timestamp === 'Unknown' || !timestamp) return 'Unknown Time';
    try {
        const date = new Date(parseInt(timestamp) * 1000);
        return date.toLocaleString('en-US', {
            year: 'numeric',
            month: '2-digit',
            day: '2-digit',
            hour: '2-digit',
            minute: '2-digit',
            second: '2-digit'
        });
    } catch (e) {
        return `ID: ${timestamp}`;
    }
}

function deleteImage(filename) {
    if (!confirm(`Are you sure you want to delete ${filename}?`)) {
        return;
    }

    fetch(`/api/snapshots/${filename}`, {
        method: 'DELETE'
    })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                alert('✓ Image deleted successfully!');
                refreshGallery();
            } else {
                alert('✗ Failed to delete image: ' + (data.error || 'Unknown error'));
            }
        })
        .catch(error => {
            console.error('Error deleting image:', error);
            alert('✗ Error deleting image. Please try again.');
        });
}

function refreshGallery() {
    loadSnapshots();
}

function openModal(filename) {
    let modal = document.getElementById('imageModal');

    if (!modal) {
        modal = document.createElement('div');
        modal.id = 'imageModal';
        modal.className = 'modal';
        modal.innerHTML = `
            <span class="close-modal" onclick="closeModal()">&times;</span>
            <img class="modal-content" id="modalImage">
        `;
        document.body.appendChild(modal);
    }

    const modalImg = document.getElementById('modalImage');
    modalImg.src = `/snapshots/${filename}`;
    modal.style.display = 'block';
}

function closeModal() {
    const modal = document.getElementById('imageModal');
    if (modal) {
        modal.style.display = 'none';
    }
}

window.onclick = function (event) {
    const modal = document.getElementById('imageModal');
    if (event.target === modal) {
        closeModal();
    }
};

function printCameraPDF() {
    const element = document.createElement('div');
    element.style.padding = '20px';
    element.style.backgroundColor = 'white';

    const duringGallery = document.getElementById('duringFeedingGallery').cloneNode(true);
    const afterGallery = document.getElementById('afterFeedingGallery').cloneNode(true);

    duringGallery.querySelectorAll('button').forEach(btn => btn.remove());
    afterGallery.querySelectorAll('button').forEach(btn => btn.remove());

    element.innerHTML = `
        <h1 style="color: #ff6b35; text-align: center;">Camera Monitoring Report</h1>
        <p style="text-align: center; color: #666;">Generated on: ${new Date().toLocaleString()}</p>
        <hr style="margin: 20px 0;">
        <h2 style="color: #ff6b35;">During Feeding Images</h2>
        <div style="display: grid; grid-template-columns: repeat(3, 1fr); gap: 15px; margin-bottom: 30px;">
            ${duringGallery.innerHTML}
        </div>
        <h2 style="color: #ff6b35;">After Feeding Images</h2>
        <div style="display: grid; grid-template-columns: repeat(3, 1fr); gap: 15px;">
            ${afterGallery.innerHTML}
        </div>
    `;

    html2pdf()
        .set({
            margin: 10,
            filename: `camera-monitoring-${new Date().toISOString().split('T')[0]}.pdf`,
            image: { type: 'jpeg', quality: 0.98 },
            html2canvas: { scale: 2 },
            jsPDF: { unit: 'mm', format: 'a4', orientation: 'portrait' }
        })
        .from(element)
        .save();
}