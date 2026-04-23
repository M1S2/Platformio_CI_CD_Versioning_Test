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
}