let updateProgressSource = null;

function bodyLoaded()
{
	initUpdateProgressEvents();
	getUpdateStatusAndInfo();
	checkUpdate();
}

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
		const updateStatus = event.data;
		const updateStatusElement = document.getElementById('update_status');
		if (updateStatusElement)
		{
			updateStatusElement.innerText = updateStatus;
		}
	});
	updateProgressSource.onerror = () =>
	{
		console.warn('EventSource connection for updateProgress failed or closed');
	};
}

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
			// Create column for this component
			const columnClone = columnTemplate.content.cloneNode(true);
			const columnTitle = columnClone.querySelector('.update-column-title');
			const cardsContainer = columnClone.querySelector('.update-component-instance-card-container');

			if (columnTitle)
			{
				columnTitle.textContent = component.name;
			}

			// Create a card for each current version
			if (component.currentVersions && component.currentVersions.length > 0)
			{
				component.currentVersions.forEach((currentVersion, versionIndex) =>
				{
					const cardClone = cardTemplate.content.cloneNode(true);
					const currentVersionElement = cardClone.querySelector('#update-component-instance-current-version');
					const availableVersionElement = cardClone.querySelector('#update-component-instance-available-version');
					const buttonStartUpdate = cardClone.querySelector('#btn-update-start');

					if (currentVersionElement)
					{
						currentVersionElement.id = `update-component-instance-${component.name}-${versionIndex}-current-version`;
						currentVersionElement.textContent = currentVersion;
					}

					if (availableVersionElement)
					{
						availableVersionElement.id = `update-component-instance-${component.name}-${versionIndex}-available-version`;
						availableVersionElement.textContent = component.available ? (component.version || '-') : '?';
					}

					if (buttonStartUpdate)
					{
						buttonStartUpdate.onclick = () => startUpdate(component.name, versionIndex);
					}

					cardsContainer.appendChild(cardClone);
				});
			}
			else
			{
				// If no current versions, create one card with "-"
				const cardClone = cardTemplate.content.cloneNode(true);
				const currentVersionElement = cardClone.querySelector('#update-component-instance-current-version');
				const availableVersionElement = cardClone.querySelector('#update-component-instance-available-version');
				const buttonStartUpdate = cardClone.querySelector('#btn-update-start');

				if (currentVersionElement)
				{
					currentVersionElement.id = `update-component-instance-${component.name}-none-current-version`;
					currentVersionElement.textContent = '-';
				}

				if (availableVersionElement)
				{
					availableVersionElement.id = `update-component-instance-${component.name}-none-available-version`;
					availableVersionElement.textContent = component.available ? (component.version || '-') : '?';
				}

				if (buttonStartUpdate)
				{
					buttonStartUpdate.style.display = 'none'; // Hide update button if no current version
				}

				cardsContainer.appendChild(cardClone);
			}

			container.appendChild(columnClone);
		});
	}
}

function displayUpdateStatus(updateStatus)
{
	const updateStatusElement = document.getElementById('update_status');
	const updateChannel = document.getElementById('update_channel');
	const progressBar = document.getElementById('update_progress');

	if (updateChannel)
	{
		updateChannel.innerText = updateStatus.channel || '-';
	}

	if (updateStatusElement)
	{
		if (updateStatus.isUpdating)
		{
			updateStatusElement.innerText = 'Updating ' + updateStatus.updateStep;
		}
		else if (updateStatus.isFetching)
		{
			updateStatusElement.innerText = 'Fetching';
		}
		else
		{
			updateStatusElement.innerText = 'Idle';
		}
	}

	if (progressBar && typeof updateStatus.updateProgress === 'number')
	{
		progressBar.value = updateStatus.updateProgress;
	}
}

async function setUpdateChannel(channel)
{
	await fetch(`/update/set_channel?channel=${encodeURIComponent(channel)}`);
  	getUpdateStatusAndInfo();
}

async function checkUpdate()
{
	const updateStatusElement = document.getElementById('update_status');

  	await fetch('/update/check')
	.then(async response =>
	{
		updateStatusElement.innerText = await response.text();
		if (response.ok)
		{
			let updateStatus;
			do
			{
				await new Promise(resolve => setTimeout(resolve, 500));
				const res = await fetch('/update/status');
				updateStatus = await res.json();
			}
			while (updateStatus.isFetching);

			await getUpdateStatusAndInfo();

			if (updateStatusElement) updateStatusElement.innerText = 'Ready';
		}
	});
}

async function startUpdate(componentName, componentIndex)
{
	try
	{
		const response = await fetch('/update/start?component=' + componentName + '&index=' + componentIndex);
		const jsonRsp = await response.json();
		const updateStatusElement = document.getElementById('update_status');
		if (updateStatusElement)
		{
			updateStatusElement.innerText = jsonRsp.message;
		}
	}
	catch (error)
	{
		console.error("Netzwerkfehler:", error);
	}
}