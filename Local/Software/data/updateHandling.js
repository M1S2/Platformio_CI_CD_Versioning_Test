/**********************************************************************/
/* Enumerations														  */
/**********************************************************************/

// Different update channels that can be used for fetching updates.
const UpdateChannels =
{
	STABLE: 0,		// Stable update channel, intended for production releases. Updates fetched from this channel are expected to be stable and reliable.
	DEV: 1			// Development update channel, intended for testing and development purposes.
};

// Different states of the update handling process. These states represent the current status of the update handling system, allowing for tracking and managing the update process.
const UpdateStates =
{
	IDLE: 0,		// The update handling system is idle and not currently performing any update-related tasks.
	CHECKING: 1,	// The update handling system is checking for available updates, typically by fetching version information from the specified update channels.
	UPDATING: 2,	// The update handling system is currently updating components.
	RESTARTING: 3,	// The update handling system is restarting the device.
	ERROR: 4		// The update handling system has encountered an error.
};

// Different steps of the update process. These steps represent the various stages of the update handling system, allowing for tracking and managing the progress of updates.
const UpdateSteps =
{
    NONE: 0,		// No update step is currently being performed.
    PREPARE: 1,		// The update handling system is preparing for the update process.
    WAIT: 2,		// The update handling system is waiting for a specific condition or event before proceeding with the update process.
	FW: 3,			// The update handling system is performing a firmware update.
	BACKUP: 4,		// The update handling system is backing up the current state.
	FS: 5,			// The update handling system is performing a filesystem update.
	RESTORE: 6,		// The update handling system is restoring the backed-up state.
	RESTART: 7,		// The update handling system is restarting the device.
	FINISHED: 8		// The update handling system has finished the update process.
};

/**********************************************************************/
/* State variables													  */
/**********************************************************************/

let lastUpdateState = UpdateStates.IDLE;		// State variables to track the last known update state.
let lastUpdateStep = UpdateSteps.NONE;			// State variable to track the last known update step.
let currentlyFetchingUpdateInfo = false;		// Flag to indicate whether the system is currently fetching update information.

/**********************************************************************/
/* Initialize														  */
/**********************************************************************/

// This function must be called when the HTML is embedded into the main HTML.
// Pass the name of the element to this function, into which the updateHandling.html should be embedded.
async function initUpdateHandling(parentElementId)
{
	const response = await fetch('updateHandling.html');
    const html = await response.text();
    document.getElementById(parentElementId).innerHTML = html;

	currentlyFetchingUpdateInfo = false;
	pollUpdateInfo();
	pollUpdateStatus(true);
	checkUpdate();
}

/**********************************************************************/
/* IndexedDB helpers for Backup/Restore								  */
/**********************************************************************/

const BACKUP_DB_NAME = 'test-update-backup';	// Name of the IndexedDB database used for storing backup snapshots during the update process.

// Open the IndexedDB database for storing backup snapshots.
// If the database does not exist, it will be created with the necessary object store and index. 
async function updateBackup_OpenDB()
{
	return new Promise((res, rej) =>
	{
		const r = indexedDB.open(BACKUP_DB_NAME, 1);
		r.onupgradeneeded = e =>
		{
			const db = e.target.result;
			if (!db.objectStoreNames.contains('snapshots'))
			{
				const store = db.createObjectStore('snapshots', { keyPath: 'id' });
				store.createIndex('timestamp', 'timestamp');
			}
		};
		r.onsuccess = () => res(r.result);
		r.onerror = () => rej(r.error);
	});
}

// Delete the backup IndexedDB database
async function updateBackup_DeleteDB()
{
	return new Promise((res, rej) =>
	{
		const r = indexedDB.deleteDatabase(BACKUP_DB_NAME);
		r.onsuccess = () => res();
		r.onerror = () => rej(r.error);
	});
}

// Save a snapshot to the IndexedDB database
async function updateBackup_SaveSnapshot(id, files, meta)
{
	const db = await updateBackup_OpenDB();
	return new Promise((res, rej) =>
	{
		const tx = db.transaction('snapshots', 'readwrite');
		const store = tx.objectStore('snapshots');
		store.put({ id, timestamp: Date.now(), meta: meta || {}, files });
		tx.oncomplete = () => { db.close(); res(); };
		tx.onerror = () => { db.close(); rej(tx.error || new Error('Transaction failed')); };
	});
}

// List all available snapshots in the IndexedDB
async function updateBackup_ListSnapshots()
{
	const db = await updateBackup_OpenDB();
	return new Promise((res, rej) =>
	{
		const tx = db.transaction('snapshots', 'readonly');
		const store = tx.objectStore('snapshots');
		const req = store.getAll();
		req.onsuccess = () => { db.close(); res(req.result || []); };
		req.onerror = () => { db.close(); rej(req.error); };
	});
}

// Get the snapshot with the newest date
async function updateBackup_GetLatestSnapshot()
{
	const snaps = await updateBackup_ListSnapshots();
	if (!snaps || snaps.length === 0) return null;
	snaps.sort((a,b) => b.timestamp - a.timestamp);
	return snaps[0];
}

// Delete one specific snapshot
async function updateBackup_DeleteSnapshot(id)
{
	const db = await updateBackup_OpenDB();
	return new Promise((res, rej) =>
	{
		const tx = db.transaction('snapshots', 'readwrite');
		const store = tx.objectStore('snapshots');
		const req = store.delete(id);
		tx.oncomplete = () => { db.close(); res(); };
		tx.onerror = () => { db.close(); rej(tx.error || new Error('Transaction failed')); };
	});
}

// Try to request persistent storage if available (useful on mobile)
async function updateBackup_EnsurePersistentStorage()
{
	try
	{
		if (navigator.storage && navigator.storage.persist)
		{
			const granted = await navigator.storage.persist();
			console.log('Storage.persist granted:', granted);
		}
	} catch (e) { console.warn('persist() not available', e); }
}

// Perform automatic backup used during update flow
async function updatePerformBackup(meta)
{
	try
	{
		await updateBackup_EnsurePersistentStorage();
		
		// Fetch the list of files to backup from the server
		const filesRes = await fetch('/fs/backup_file_list');
		if (!filesRes.ok) throw new Error('Failed to fetch backup file list: ' + filesRes.status);
		const files = await filesRes.json();
		if (!Array.isArray(files) || files.length === 0) throw new Error('No files available for backup');
		
		const entries = [];
		for (const fn of files)
		{
			const res = await fetch('/fs/download?file=' + encodeURIComponent(fn));
			if (!res.ok) throw new Error('Failed to download file ' + fn + ': ' + res.status);
			const data = await res.arrayBuffer();
			entries.push({ name: fn, blob: new Blob([data]) });
		}
		const id = 'backup-latest';
		await updateBackup_SaveSnapshot(id, entries, Object.assign({ source: 'auto', count: entries.length }, meta || {}));
		console.log('Auto-backup saved to IndexedDB with id', id);
	}
	catch (err)
	{
		console.error('performBackup failed', err);
		throw err;
	}
	finally
	{
		await fetch('/fs/backup_confirm', { method: 'POST' });
	}
}

// Perform automatic restore used during update flow
async function updatePerformRestore()
{
	try
	{
		const snap = await updateBackup_GetLatestSnapshot();
		if (!snap)
		{
			console.warn('No backup snapshots available in IndexedDB');
			await fetch('/fs/restore_confirm', { method: 'POST' });
			return;
		}
		console.log('Auto-restoring snapshot', snap.id, 'with', (snap.files || []).length, 'files');
		for (const file of snap.files)
		{
			const blob = (file.blob instanceof Blob) ? file.blob : new Blob([file.blob]);
			const form = new FormData();
			form.append('file', blob, file.name);
			await fetch('/fs/upload', { method: 'POST', body: form });
		}
		try
		{
			await updateBackup_DeleteDB();
			console.log('Deleted backup database after restore:', snap.id);
		}
		catch (err)
		{
			console.error('Failed to delete backup snapshot after restore', err);
		}
	}
	catch (err)
	{
		console.error('performRestore failed', err);
		throw err;
	}
	finally
	{
		await fetch('/fs/restore_confirm', { method: 'POST' });
	}
}

/**********************************************************************/
/* Enumerations to String/Icon helpers								  */
/**********************************************************************/

// Get a string corresponding to the UpdateStep
function updateStepToString(step)
{
    switch (step)
	{
        case UpdateSteps.NONE:     return '-';
        case UpdateSteps.PREPARE:  return 'Preparing Update';
        case UpdateSteps.WAIT:     return 'Waiting';
		case UpdateSteps.FW:       return 'Firmware Update';
		case UpdateSteps.BACKUP:   return 'Backing up Files';
		case UpdateSteps.FS:       return 'Filesystem Update';
		case UpdateSteps.RESTORE:  return 'Restoring Files';
        case UpdateSteps.FINISHED: return 'Finished Update';
		case UpdateSteps.RESTART:  return 'Restarting';
        default: return '?';
    }
}

// Get a string used to get an icon for the UpdateStep
function getUpdateStepIcon(step)
{
    switch(step)
    {
        case UpdateSteps.PREPARE: return "build";
        case UpdateSteps.WAIT: return "schedule";
        case UpdateSteps.FW: return "memory";
		case UpdateSteps.BACKUP: return "backup";
		case UpdateSteps.FS: return "folder";
		case UpdateSteps.RESTORE: return "restore";
        case UpdateSteps.RESTART: return "restart_alt";
		case UpdateSteps.FINISHED: return "check_circle";
        default: return "circle";
    }
}

/**********************************************************************/
/* Polling functions												  */
/**********************************************************************/

// Poll the update status from the backend.
// This function calls itself after a period of time when cyclic is true.
async function pollUpdateStatus(cyclic = true)
{
	try
	{
		const statusResponse = await fetch('/update/status');
		if (statusResponse.ok)
		{
			const updateStatus = await statusResponse.json();
			displayUpdateStatus(updateStatus);

			// Reload the update infos to get the new versions:
			// - If a check for updates has finished
			// - If an update has finished (and thus a new version is now available)
			// - If a restart has finished
			if (currentlyFetchingUpdateInfo ||
				(lastUpdateState === UpdateStates.CHECKING && updateStatus.state === UpdateStates.IDLE) ||
			   	(lastUpdateState === UpdateStates.UPDATING && updateStatus.state === UpdateStates.IDLE) ||
			    (lastUpdateState === UpdateStates.RESTARTING && updateStatus.state === UpdateStates.IDLE))
			{
				currentlyFetchingUpdateInfo = false;
				await pollUpdateInfo();
			}
			// Only when the update step changes
			if(updateStatus.updateStep !== lastUpdateStep)
			{
				// Fetch the remaining tasks and update the task list display whenever the update step changes
				const remainingTasksRes = await fetch('/update/remaining_tasks');
				if (remainingTasksRes.ok)
				{
					const remainingTasks = await remainingTasksRes.json();
					displayUpdateTaskList(remainingTasks);
				}
				else
				{
					console.error('Failed to fetch remaining tasks:', remainingTasksRes.status);
				}

				// Trigger automatic backup/restore when the update flow reaches those steps
				if (updateStatus.updateStep === UpdateSteps.BACKUP)
				{
					updatePerformBackup({ component: updateStatus.currentComponent, instance: updateStatus.currentComponentInstanceIndex })
						.catch(err => console.error('Auto backup failed', err));
				}
				if (updateStatus.updateStep === UpdateSteps.RESTORE)
				{
					updatePerformRestore()
						.catch(err => console.error('Auto restore failed', err));
				}
			}
			lastUpdateState = updateStatus.state;
			lastUpdateStep = updateStatus.updateStep;
		}
	}
	catch (e) { console.error('Status polling error:', e); }
	if (cyclic)
	{
		// Use setTimeout to poll again after a delay, instead of setInterval, to avoid overlapping calls if one takes too long
		setTimeout(pollUpdateStatus, 1000);
	}
}

/**********************************************************************/

// Poll the update info from the backend.
async function pollUpdateInfo()
{
	try
	{
		const infoResponse = await fetch('/update/info');
		if (infoResponse.ok)
		{
			const updateInfo = await infoResponse.json();
			displayUpdateInfos(updateInfo);
		}
	}
	catch (e) { console.error('Status polling error:', e); }
}

/**********************************************************************/
/* Display functions 												  */
/**********************************************************************/

// Display the update info on the HTML frontend.
// The needed elements are generated dynamically.
function displayUpdateInfos(updateInfo)
{
	const container = document.getElementById('update-components-container');
	const columnTemplate = document.getElementById('update-component-column-template');
	const cardTemplate = document.getElementById('update-component-instance-card-template');

	if (!container || !columnTemplate || !cardTemplate)
	{
		console.error('Container or templates not found');
		return;
	}

	// Clear existing components
	container.innerHTML = '';

	// Generate component columns from template
	if (updateInfo.components && Array.isArray(updateInfo.components))
	{
		updateInfo.components.forEach(component =>
		{
			// If no current versions are available, skip this component
			if (!component.currentVersions || component.currentVersions.length === 0 || component.instance_count === 0)
			{
				return;	// Acts like continue; for the .forEach() loop.
			}

			// Create column for this component
			const columnClone = columnTemplate.content.cloneNode(true);
			const columnTitle = columnClone.querySelector('.update-column-title');
			const cardsContainer = columnClone.querySelector('.update-component-instance-card-container');

			if (columnTitle) columnTitle.textContent = component.name;

			// Create a card for each current version
			component.currentVersions.forEach((currentVersion, versionIndex) =>
			{
				const cardClone = cardTemplate.content.cloneNode(true);
				const currentVersionElement = cardClone.querySelector('#update-component-instance-current-version');
				const availableVersionElement = cardClone.querySelector('#update-component-instance-available-version');
				const buttonStartUpdate = cardClone.querySelector('#btn-update-start');
				const loaderBtnStartUpdate = cardClone.querySelector('#loader-btn-update-start');

				if (currentVersionElement)
				{
					currentVersionElement.id = `update-component-instance-current-version_${component.name}_${versionIndex}`;
					currentVersionElement.textContent = currentVersion;
				}

				if (availableVersionElement)
				{
					availableVersionElement.id = `update-component-instance-available-version_${component.name}_${versionIndex}`;
					availableVersionElement.textContent = component.available ? (component.version || '-') : '?';
				}

				if (buttonStartUpdate)
				{
					buttonStartUpdate.id = `btn-update-start_${component.name}_${versionIndex}`;
					buttonStartUpdate.onclick = () => startUpdate(component.name, versionIndex);
				}

				if (loaderBtnStartUpdate) loaderBtnStartUpdate.id = `loader-btn-update-start_${component.name}_${versionIndex}`;

				cardsContainer.appendChild(cardClone);
			});
			container.appendChild(columnClone);
		});
	}
}

/**********************************************************************/

// Display the update status on the HTML frontend.
function displayUpdateStatus(updateStatus)
{
	const updateStatusElement = document.querySelector('.update-status');
	const updateChannelSelect = document.getElementById('update_channel_select');
	const progressBar = document.querySelector('.update-progress');
	const progressBarPercentage = document.querySelector('.update-progress-percent');

	const btnCheckUpdate = document.getElementById('btn-check-update');
	const loaderBtnCheckUpdate = document.getElementById('loader-btn-check-update');

	if (updateChannelSelect)
	{
		updateChannelSelect.value = (updateStatus.channel === UpdateChannels.DEV) ? 'dev' : 'stable';
	}

	const componentText = updateStatus.currentComponentName;
	let statusText = 'Unknown';
	let statusIcon = 'circle';
	let isChecking = false;
	let isUpdating = false;
	switch (updateStatus.state)
	{
		case UpdateStates.IDLE:
			statusText = 'Ready';
			statusIcon = 'check_circle';
			break;
		case UpdateStates.CHECKING:
			statusText = 'Checking for new versions';
			statusIcon = 'search';
			isChecking = true;
			break;
		case UpdateStates.UPDATING:
			const updateStepText = updateStepToString(updateStatus.updateStep);
			statusIcon = getUpdateStepIcon(updateStatus.updateStep);
			switch(updateStatus.updateStep)
			{
				case UpdateSteps.NONE:
					statusText = updateStepText;
					break;
				case UpdateSteps.WAIT:
					statusText = updateStepText + ` for ${componentText}`;
					progressBar.removeAttribute("value"); 	// set the progressbar to indeterminate
					progressBarPercentage.textContent = '';
					break;
				case UpdateSteps.PREPARE:
				case UpdateSteps.FW:
				case UpdateSteps.FS:
				case UpdateSteps.BACKUP:
				case UpdateSteps.RESTORE:
				case UpdateSteps.FINISHED:
					statusText = updateStepText + ` of ${componentText}`;
					break;
				case UpdateSteps.RESTART:
					statusText = updateStepText + ` of ${componentText}`;
					progressBar.removeAttribute("value"); 	// set the progressbar to indeterminate
					progressBarPercentage.textContent = '';
					break;
				default:
					statusText = 'Updating';
					break;
			}
			isUpdating = true;
			break;
		case UpdateStates.RESTARTING:
			statusText = 'Restarting device';
			progressBar.removeAttribute("value"); 	// set the progressbar to indeterminate
			progressBarPercentage.textContent = '';
			statusIcon = 'restart_alt';
			isUpdating = true;
			break;
		case UpdateStates.ERROR:
			statusText = 'Error';
			statusIcon = 'error';
			break;
		default:
			break;
	}
	if(updateStatusElement)
	{
		updateStatusElement.innerHTML = '';	// Clear existing content

		const icon = document.createElement("span");
		icon.className = "material-symbols-outlined";
		icon.textContent = statusIcon;
		
		const text = document.createElement("span");
		text.textContent = statusText;

		updateStatusElement.appendChild(icon);
		updateStatusElement.appendChild(text);
	}
	if(btnCheckUpdate) btnCheckUpdate.disabled = isChecking || isUpdating;
	if(loaderBtnCheckUpdate) loaderBtnCheckUpdate.style.display = isChecking ? 'inline-block' : 'none';

	updateChannelSelect.disabled = isChecking || isUpdating;

	// Handle dynamic component update buttons and loaders by ID prefix
	const componentUpdateButtons = document.querySelectorAll('[id^="btn-update-start_"]');
	componentUpdateButtons.forEach(btn =>
	{
		btn.disabled = isChecking || isUpdating;
	});
	const componentUpdateLoaders = document.querySelectorAll('[id^="loader-btn-update-start_"]');
	componentUpdateLoaders.forEach(loader =>
	{
		// Check, if this loader is part of the current component
		const isCurrentActiveLoader = isUpdating &&
									  updateStatus.currentComponentName &&
									  loader.id.startsWith(`loader-btn-update-start_${updateStatus.currentComponentName}_${updateStatus.currentComponentInstanceIndex}`);
		loader.style.display = isCurrentActiveLoader ? 'inline-block' : 'none';
	});

	if (progressBar && typeof updateStatus.updateProgress === 'number' && updateStatus.updateStep !== UpdateSteps.WAIT)
	{
		progressBar.value = updateStatus.updateProgress;
		progressBarPercentage.textContent = `${Math.round(updateStatus.updateProgress)} %`;
	}
}

/**********************************************************************/

// Display a list with all (remaining) update tasks
function displayUpdateTaskList(updateTaskList)
{
	const container = document.getElementById("update_task_list");
    container.innerHTML = "";
    if(updateTaskList.length === 0)
    {
        container.innerHTML = '<div class="update-task-empty">keine ausstehenden Tasks</div>';
        return;
    }

    const groupedTasks = {};
    updateTaskList.forEach(task =>
    {
        const key = `${task.componentName}_${task.instance}`;
        if(!groupedTasks[key])
        {
            groupedTasks[key] =
            {
                componentName: task.componentName,
                instance: task.instance,
                steps: []
            };
        }
        groupedTasks[key].steps.push(task.step);
    });

    Object.values(groupedTasks).forEach(group =>
    {
        const groupDiv = document.createElement("div");
        groupDiv.className = "update-task-group";

        const header = document.createElement("div");
        header.className = "update-task-group-header";
		header.textContent = group.instance >= 0 ? `${group.componentName} #${group.instance}` : group.componentName;
		groupDiv.appendChild(header);

        group.steps.forEach(step =>
        {
            const row = document.createElement("div");
            row.className = "update-task-step";
            
			const icon = document.createElement("span");
            icon.className = "material-symbols-outlined";
            icon.textContent = getUpdateStepIcon(step);
            
			const text = document.createElement("span");
            text.textContent = updateStepToString(step);

            row.appendChild(icon);
            row.appendChild(text);
            groupDiv.appendChild(row);
        });

        container.appendChild(groupDiv);
    });
}

/**********************************************************************/
/* UI Element actions 												  */
/**********************************************************************/

// Change the update channel
async function setUpdateChannel(channel)
{
	await fetch(`/update/set_channel`,
	{
		method: 'POST',
		headers:
		{
			'Content-Type': 'application/json'
		},
		body: JSON.stringify({ channel: channel })
	});
	pollUpdateInfo();
}

/**********************************************************************/

// Trigger a new check for updates
async function checkUpdate()
{
	await fetch('/update/check', { method: 'POST' });
	currentlyFetchingUpdateInfo = true;
}

/**********************************************************************/

// Start the update for a specific component
async function startUpdate(componentName, componentInstanceIndex)
{
	try
	{
		const response = await fetch('/update/start',
		{
			method: 'POST',
			headers:
			{
				'Content-Type': 'application/json'
			},
			body: JSON.stringify(
			{
				componentName: componentName,
				componentInstanceIndex: componentInstanceIndex
			})
		});
		const jsonRsp = await response.json();
		const updateStatusElement = document.getElementById('update_status');
		if (updateStatusElement && jsonRsp.status === 'error')
		{
			updateStatusElement.innerText = 'Error: ' + jsonRsp.message;
		}
	}
	catch (error)
	{
		console.error("Netzwerkfehler:", error);
	}
}