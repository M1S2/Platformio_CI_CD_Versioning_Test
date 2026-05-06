let updateProgressSource = null;

function bodyLoaded()
{
	fetch('/test')
	.then(response => response.json())
	.then(testData =>
	{
		const testText = document.getElementById('testText');
		testText.textContent = testData.text;

		const testVersion = document.getElementById('testVersion');
		testVersion.textContent = testData.version;
	});

	initUpdateProgressEvents();
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

	displayUpdateStatus(updateStatus);
	displayUpdateInfos(updateInfo);
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

function displayUpdateInfos(updateInfo)
{
	const part1CurrentVersion = document.getElementById('part1-current-version');
	const part1AvailableVersion = document.getElementById('part1-available-version');

	if (part1CurrentVersion) part1CurrentVersion.innerText = (updateInfo.part1.currentVersions && updateInfo.part1.currentVersions.length > 0) ? updateInfo.part1.currentVersions[0] : '-';
	if (part1AvailableVersion) part1AvailableVersion.innerText = updateInfo.part1.available ? (updateInfo.part1.version || '-') : '?';

	const part2CurrentVersion = document.getElementById('part2-current-version');
	const part2AvailableVersion = document.getElementById('part2-available-version');

	if (part2CurrentVersion) part2CurrentVersion.innerText = (updateInfo.part2.currentVersions && updateInfo.part2.currentVersions.length > 0) ? updateInfo.part2.currentVersions[0] : '-';
	if (part2AvailableVersion) part2AvailableVersion.innerText = updateInfo.part2.available ? (updateInfo.part2.version || '-') : '?';

	/*data.sensors.forEach(sensor => {
		const el = document.createElement('div');
		el.className = 'sensor-card';

		el.innerHTML = `
		<h3>${sensor.name}</h3>
		<p>Aktuell: ${sensor.current}</p>
		<p>Neu: ${sensor.available || '-'}</p>
		<button onclick="updateSensor('${sensor.id}')">Update</button>
		<div class="progress">
			<div class="progress-bar" id="sensor-progress-${sensor.id}"></div>
		</div>
		`;

		container.appendChild(el);
	});*/
}

async function setUpdateChannel(channel)
{
  await fetch(`/update/set_channel?channel=${encodeURIComponent(channel)}`);
  getUpdateStatusAndInfo();
}

async function checkUpdate()
{
  const updateStatusElement = document.getElementById('update_status');
  if (updateStatusElement) updateStatusElement.innerText = 'Checking...';

  await fetch('/update/check');

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