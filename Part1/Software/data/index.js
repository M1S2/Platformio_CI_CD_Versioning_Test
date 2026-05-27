const UpdateChannels =
{
	STABLE: 0,
	DEV: 1
};

const UpdateComponents =
{
    UPDATE_COMPONENT_NONE: 0,
    UPDATE_COMPONENT_PART1: 1,
    UPDATE_COMPONENT_PART2: 2
};

const UpdateStates =
{
	IDLE: 0,
	CHECKING: 1,
	UPDATING: 2,
	RESTARTING: 3,
	ERROR: 4
};

const UpdateSteps =
{
    NONE: 0,
    PREPARE: 1,
    WAIT: 2,
    FW: 3,
    FS: 4,
    FINISHED: 5
};

let lastUpdateState = UpdateStates.IDLE;
let lastUpdateStep = UpdateSteps.NONE;
let currentlyFetchingUpdateInfo = false;

/**********************************************************************/

function bodyLoaded()
{
	currentlyFetchingUpdateInfo = false;
	pollUpdateInfo();
	pollUpdateStatus(true);
	checkUpdate();
}

/**********************************************************************/

async function pollUpdateStatus(cyclic = true)
{
	try
	{
		const statusRes = await fetch('/update/status');
		if (statusRes.ok)
		{
			const updateStatus = await statusRes.json();
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
			if(updateStatus.updateStep !== lastUpdateStep)
			{
				const remainingTasksRes = await fetch('/update/remaining_tasks');
				if (remainingTasksRes.ok)
				{
					const remainingTasks = await remainingTasksRes.json();
					console.log('Remaining update tasks:', remainingTasks);
				}
				else
				{
					console.error('Failed to fetch remaining tasks:', remainingTasksRes.status);
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

async function pollUpdateInfo()
{
	try
	{
		const infoRes = await fetch('/update/info');
		if (infoRes.ok)
		{
			const updateInfo = await infoRes.json();
			displayUpdateInfos(updateInfo);
		}
	}
	catch (e) { console.error('Status polling error:', e); }
}

/**********************************************************************/

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
			if (!component.currentVersions || component.currentVersions.length === 0)
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
					currentVersionElement.id = `update-component-instance-current-version_${component.id}_${versionIndex}`;
					currentVersionElement.textContent = currentVersion;
				}

				if (availableVersionElement)
				{
					availableVersionElement.id = `update-component-instance-available-version_${component.id}_${versionIndex}`;
					availableVersionElement.textContent = component.available ? (component.version || '-') : '?';
				}

				if (buttonStartUpdate)
				{
					buttonStartUpdate.id = `btn-update-start_${component.id}_${versionIndex}`;
					buttonStartUpdate.onclick = () => startUpdate(component.name, versionIndex);
				}

				if (loaderBtnStartUpdate) loaderBtnStartUpdate.id = `loader-btn-update-start_${component.id}_${versionIndex}`;

				cardsContainer.appendChild(cardClone);
			});
			container.appendChild(columnClone);
		});
	}
}

/**********************************************************************/

function displayUpdateStatus(updateStatus)
{
	const updateStatusElement = document.getElementById('update_status');
	const updateChannel = document.getElementById('update_channel');
	const progressBar = document.getElementById('update_progress');

	const btnCheckUpdate = document.getElementById('btn-check-update');
	const loaderBtnCheckUpdate = document.getElementById('loader-btn-check-update');

	if (updateChannel)
	{
		let channelText = 'Unknown';
		if(updateStatus.channel === UpdateChannels.STABLE)
		{
			channelText = 'Stable';
		}
		else if(updateStatus.channel === UpdateChannels.DEV)
		{
			channelText = 'Dev';
		}
		updateChannel.innerText = channelText;
	}

	let componentText = '?';
	switch (updateStatus.currentComponent)
	{
		case UpdateComponents.UPDATE_COMPONENT_NONE:
			componentText = 'None';
			break;
		case UpdateComponents.UPDATE_COMPONENT_PART1:
			componentText = 'Part 1';
			break;
		case UpdateComponents.UPDATE_COMPONENT_PART2:
			componentText = 'Part 2';
			break;
		default:
			break;
	}

	let statusText = 'Unknown';
	let isChecking = false;
	let isUpdating = false;
	switch (updateStatus.state)
	{
		case UpdateStates.IDLE:
			statusText = 'Ready';
			break;
		case UpdateStates.CHECKING:
			statusText = 'Checking for new versions';
			isChecking = true;
			break;
		case UpdateStates.UPDATING:
			switch(updateStatus.updateStep)
			{
				case UpdateSteps.NONE:
					statusText = 'Updating';
					break;
				case UpdateSteps.PREPARE:
					statusText = `Preparing update of ${componentText}`;
					break;
				case UpdateSteps.WAIT:
					statusText = `Waiting for ${componentText}`;
					progressBar.removeAttribute("value"); 	// set the progressbar to indeterminate
					break;
				case UpdateSteps.FW:
					statusText = `Updating firmware of ${componentText}`;
					break;
				case UpdateSteps.FS:
					statusText = `Updating filesystem of ${componentText}`;
					break;
				case UpdateSteps.FINISHED:
					statusText = `Update of ${componentText} finished`;
					break;
				default:
					statusText = 'Updating';
					break;
			}
			isUpdating = true;
			break;
		case UpdateStates.RESTARTING:
			statusText = 'Restarting device';
			isUpdating = true;
			break;
		case UpdateStates.ERROR:
			statusText = 'Error';
			break;
		default:
			break;
	}
	if(updateStatusElement) updateStatusElement.innerText = statusText;
	if(btnCheckUpdate) btnCheckUpdate.disabled = isChecking || isUpdating;
	if(loaderBtnCheckUpdate) loaderBtnCheckUpdate.style.display = isChecking ? 'inline-block' : 'none';

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
									  updateStatus.currentComponent &&
									  loader.id.startsWith(`loader-btn-update-start_${updateStatus.currentComponent}_${updateStatus.currentComponentInstanceIndex}`);
		loader.style.display = isCurrentActiveLoader ? 'inline-block' : 'none';
	});

	if (progressBar && typeof updateStatus.updateProgress === 'number' && updateStatus.updateStep !== UpdateSteps.WAIT)
	{
		progressBar.value = updateStatus.updateProgress;
	}
}

/**********************************************************************/

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

async function checkUpdate()
{
	await fetch('/update/check', { method: 'POST' });
	currentlyFetchingUpdateInfo = true;
}

/**********************************************************************/

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
				component: componentName,
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