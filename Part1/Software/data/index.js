const UpdateChannel =
{
	STABLE: 0,
	DEV: 1
};

const UpdateState =
{
	IDLE: 0,
	CHECKING: 1,
	UPDATING: 2,
	RESTARTING: 3,
	ERROR: 4
};

const UpdateStep =
{
    NONE: 0,
    PREPARE: 1,
    WAIT: 2,
    FW: 3,
    FS: 4,
    FINISHED: 5
};

let updateProgressSource = null;

/**********************************************************************/

function bodyLoaded()
{
	initUpdateProgressEvents();
	getUpdateStatusAndInfo();
	checkUpdate();
}

/**********************************************************************/

function initUpdateProgressEvents()
{
	if (updateProgressSource)
	{
		return;
	}

	updateProgressSource = new EventSource('/events');
	updateProgressSource.addEventListener('updateProgress', event =>
	{
		const progressValue = parseFloat(event.data);
		if (!Number.isNaN(progressValue))
		{
			const progressBar = document.getElementById('update_progress');
			if (progressBar)
			{
				progressBar.value = progressValue;
			}
		}
	});
	updateProgressSource.addEventListener('updateStatus', event =>
	{
		try
		{
			const updateStatus = JSON.parse(event.data);
			displayUpdateStatus(updateStatus);
		}
		catch (e)
		{
			console.error('Failed to parse updateStatus JSON:', e);
		}
	});
	updateProgressSource.onerror = () =>
	{
		console.warn('EventSource connection for updateProgress failed or closed');
	};
}

/**********************************************************************/

async function getUpdateStatusAndInfo()
{
	const [statusRes, infoRes] = await Promise.all([
		fetch('/update/status'),
		fetch('/update/info')
	]);

	const updateStatus = await statusRes.json();
	const updateInfo = await infoRes.json();

	displayUpdateInfos(updateInfo);
	displayUpdateStatus(updateStatus);
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
		if(updateStatus.channel === UpdateChannel.STABLE)
		{
			channelText = 'Stable';
		}
		else if(updateStatus.channel === UpdateChannel.DEV)
		{
			channelText = 'Dev';
		}
		updateChannel.innerText = channelText;
	}

	let statusText = 'Unknown';
	let isChecking = false;
	let isUpdating = false;
	switch (updateStatus.state)
	{
		case UpdateState.IDLE:
			statusText = 'Ready';
			break;
		case UpdateState.CHECKING:
			statusText = 'Checking for new versions';
			isChecking = true;
			break;
		case UpdateState.UPDATING:
			switch(updateStatus.updateStep)
			{
				case UpdateStep.NONE:
					statusText = 'Updating';
					break;
				case UpdateStep.PREPARE:
					statusText = `Preparing update of ${updateStatus.currentComponent}`;
					break;
				case UpdateStep.WAIT:
					statusText = `Waiting for ${updateStatus.currentComponent}`;
					progressBar.removeAttribute("value"); 	// set the progressbar to indeterminate
					break;
				case UpdateStep.FW:
					statusText = `Updating firmware of ${updateStatus.currentComponent}`;
					break;
				case UpdateStep.FS:
					statusText = `Updating filesystem of ${updateStatus.currentComponent}`;
					break;
				case UpdateStep.FINISHED:
					statusText = `Update of ${updateStatus.currentComponent} finished`;
					break;
				default:
					statusText = 'Updating';
					break;
			}
			isUpdating = true;
			break;
		case UpdateState.RESTARTING:
			statusText = 'Restarting device';
			isUpdating = true;
			break;
		case UpdateState.ERROR:
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

	if (progressBar && typeof updateStatus.updateProgress === 'number' && updateStatus.updateStep !== UpdateStep.WAIT)
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
  	getUpdateStatusAndInfo();
}

/**********************************************************************/

async function checkUpdate()
{
  	await fetch('/update/check', { method: 'POST' })
	.then(async response =>
	{
		if (response.ok)
		{
			let updateStatus;
			do
			{
				await new Promise(resolve => setTimeout(resolve, 500));
				const res = await fetch('/update/status');
				updateStatus = await res.json();
			}
			while (updateStatus.state === UpdateState.CHECKING);

			await getUpdateStatusAndInfo();
		}
	});
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