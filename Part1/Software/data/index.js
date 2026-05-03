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

	checkUpdate();
}


async function getUpdateStatusAndInfo()
{
	const [statusRes, infoRes] = await Promise.all([
		fetch('/update/status'),
		fetch('/update/info')
	]);

	const status = await statusRes.json();
	const info = await infoRes.json();

	displayUpdateStatus(status);
	displayUpdateInfos(info);
}

function displayUpdateStatus(status)
{
	const part1Status = document.getElementById('part1-status');
	const progressBar = document.getElementById('part1-progress');
	const updateChannel = document.getElementById('updateChannel');

	if (progressBar && typeof status.updateProgress === 'number')
	{
		progressBar.value = status.updateProgress;
	}

	if (updateChannel)
	{
		updateChannel.innerText = status.channel || '-';
	}

	if (part1Status)
	{
		if (status.isUpdating)
		{
			part1Status.innerText = 'Updating ' + status.updateStep;
		}
		else if (status.isFetching)
		{
			part1Status.innerText = 'Fetching';
		}
		else
		{
			part1Status.innerText = 'Idle';
		}
	}
}

function displayUpdateInfos(info)
{
	const part1CurrentVersion = document.getElementById('part1-current');
	const part1AvailableVersion = document.getElementById('part1-available');

	if (part1CurrentVersion) part1CurrentVersion.innerText = info.part1.currentVersion || '-';
	if (part1AvailableVersion) part1AvailableVersion.innerText = info.part1.available ? (info.part1.version || '-') : '?';

	const container = document.getElementById('part2-list');
	if (container)
	{
		container.innerHTML = '';
	}

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

async function setChannel(channel)
{
  await fetch(`/update/set_channel?channel=${encodeURIComponent(channel)}`);
  getUpdateStatusAndInfo();
}

async function checkUpdate()
{
  const part1Status = document.getElementById('part1-status');
  if (part1Status) part1Status.innerText = 'Checking...';

  await fetch('/update/check');

  let status;
  do
  {
    await new Promise(resolve => setTimeout(resolve, 500));
    const res = await fetch('/update/status');
    status = await res.json();
  }
  while (status.isFetching);

  await getUpdateStatusAndInfo();

  if (part1Status) part1Status.innerText = 'Ready';
}

async function startUpdatePart1()
{
  const part1Status = document.getElementById('part1-status');
  if (part1Status) part1Status.innerText = 'Starting update...';

  const progressBar = document.getElementById('part1-progress');
  if (progressBar) progressBar.value = 0;

  await fetch('/update/start');

  let status;
  do
  {
    await new Promise(resolve => setTimeout(resolve, 2000));
    const res = await fetch('/update/status');
    status = await res.json();
    displayUpdateStatus(status);
  }
  while (status.isUpdating);

  await getUpdateStatusAndInfo();

  if (part1Status) part1Status.innerText = 'Done';
}

//async function updateSensor(id)
//{
//  await fetch(`/sensor/${id}/update/start`);
//}