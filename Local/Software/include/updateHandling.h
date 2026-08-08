#ifndef UPDATE_HANDLING_H
#define UPDATE_HANDLING_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "genericQueue.h"

/// @brief Maximum size of the update queue (how many update tasks can be queued at once).
#define UPDATE_QUEUE_SIZE 15

/// @brief Different update channels that can be used for fetching updates. The stable channel is intended for production releases, while the development channel is for testing and development purposes.
enum UpdateChannels
{
    UPDATE_CHANNEL_STABLE,  ///< Stable update channel, intended for production releases. Updates fetched from this channel are expected to be stable and reliable.
    UPDATE_CHANNEL_DEV      ///< Development update channel, intended for testing and development purposes.
};

/// @brief Different states of the update handling process. These states represent the current status of the update handling system, allowing for tracking and managing the update process.
enum UpdateStates
{
    UPDATE_STATE_IDLE,          ///< The update handling system is idle and not currently performing any update-related tasks.
    UPDATE_STATE_CHECKING,      ///< The update handling system is checking for available updates, typically by fetching version information from the specified update channels.
    UPDATE_STATE_UPDATING,      ///< The update handling system is currently updating components.
    UPDATE_STATE_RESTARTING,    ///< The update handling system is restarting the device.
    UPDATE_STATE_ERROR          ///< The update handling system has encountered an error.
};

/// @brief Different steps of the update process. These steps represent the various stages of the update handling system, allowing for tracking and managing the progress of updates.
enum UpdateSteps
{
    UPDATE_STEP_NONE,           ///< No update step is currently being performed.
    UPDATE_STEP_PREPARE,        ///< The update handling system is preparing for the update process.
    UPDATE_STEP_WAIT,           ///< The update handling system is waiting for a specific condition or event before proceeding with the update process.
    UPDATE_STEP_FW,             ///< The update handling system is performing a firmware update.
    UPDATE_STEP_BACKUP,         ///< The update handling system is backing up the current state.
    UPDATE_STEP_FS,             ///< The update handling system is performing a filesystem update.
    UPDATE_STEP_RESTORE,        ///< The update handling system is restoring the backed-up state.
    UPDATE_STEP_RESTART,        ///< The update handling system is restarting the device.
    UPDATE_STEP_FINISHED        ///< The update handling system has finished the update process.
};

/*####################################################################*/
/*####################################################################*/

// Forward declaration of UpdateHandlingComponentBase class, update task struct and typedef (otherwise we would have a circular dependencies)
class UpdateHandlingComponentBase;
struct update_task;
typedef struct update_task update_task_t;


/// @brief Struct representing the current status of the update handling system.
typedef struct update_status
{
    UpdateChannels currentUpdateChannel = UPDATE_CHANNEL_DEV;       ///< Current update channel (stable or dev)
    UpdateStates state = UPDATE_STATE_IDLE;                         ///< Current state of the update handling
    UpdateHandlingComponentBase* currentComponent = nullptr;        ///< Pointer to the component currently being updated
    int currentComponentInstanceIndex = -1;                         ///< Index of the component instance currently being updated
    UpdateSteps updateStep = UPDATE_STEP_NONE;                      ///< Current step of the update process (firmware update or filesystem update)
    float updateProgress = 0.0f;                                    ///< Progress of the current or last firmware and filesystem update (0.0 to 100.0)
} update_status_t;

/// @brief Struct representing the update information for a specific component.
typedef struct update_info
{
    bool valid;             ///< Whether the update info is valid (i.e. whether a valid manifest was fetched and parsed)
    String version;         ///< Firmware version (e.g. "1.0.0" or "dev-SW_v2.0.0-p2-856538c")
    bool has_fs_update;     ///< Whether the update includes a filesystem update (i.e. whether the manifest contains a valid URL for the filesystem binary)
    String url_fw;          ///< Firmware URL
    String url_fs;          ///< Filesystem URL (if applicable, otherwise empty)
    String fw_md5;          ///< MD5 hash of the firmware binary for integrity check
    String fs_md5;          ///< MD5 hash of the filesystem binary for integrity check (if applicable, otherwise empty)
} update_info_t;

/**********************************************************************/

typedef bool (*update_step_handler_t)(update_task_t &task);

/// @brief Struct representing an update task in the update handling system.
struct update_task
{
    UpdateHandlingComponentBase* componentDef = nullptr;    ///< Pointer to the component instance that created this update task. This allows the update handling system to associate the task with the specific component that initiated it, enabling proper management and tracking of update tasks for different components.
    int componentInstanceIndex = -1;                        ///< Index of the component instance targeted by the update task.
    UpdateSteps step = UPDATE_STEP_NONE;                    ///< Step of the update process targeted by the update task.
    update_step_handler_t handler = nullptr;                ///< Handler function to perform the update step.
};

/*####################################################################*/
/*####################################################################*/

/// @brief Function pointer type for logging messages during the update process. This allows the update handling system to log messages to a user-defined logging function, enabling flexible logging and debugging capabilities.
typedef void (*log_function_t)(PGM_P formatP, va_list args);

/// @brief The UpdateHandling class manages the update process for multiple components, including fetching version information, enqueuing update tasks, and handling the update state machine. It provides a common interface for different update components to implement their specific update logic and web server endpoints.
class UpdateHandling
{
public:
    /// @brief Constructor for the UpdateHandling class. Initializes the update handling system with the specified base URLs for stable and development channels, as well as the manifest file name. This constructor sets up the necessary internal structures and prepares the update handling system for operation.
    /// @param stableBaseUrl The base URL for the stable update channel, where the manifest and update files for stable releases are hosted.
    /// @param devBaseUrl The base URL for the development update channel, where the manifest and update files for development releases are hosted.
    /// @param manifestName The name of the manifest file containing update information.
    /// @param logFunction Pointer to the logging function for handling update-related messages.
    UpdateHandling(const char* stableBaseUrl, const char* devBaseUrl, const char* manifestName, log_function_t logFunction = nullptr);

    /// @brief Destructor for the UpdateHandling class. Cleans up any allocated resources and ensures proper shutdown of the update handling system.
    ~UpdateHandling();

    /// @brief Initializes the web server endpoints for update handling. This function registers the necessary HTTP endpoints on the provided AsyncWebServer instance, allowing clients to interact with the update handling system via HTTP requests. It also sets up the session and certificate list for secure communication.
    /// @param p_server Pointer to the AsyncWebServer instance where the endpoints will be registered.
    /// @param p_session Pointer to the Session instance used for managing secure connections.
    /// @param p_certList Pointer to the X509List instance containing the SSL/TLS certificates for secure communication.
    void initWebserverEndpoints(AsyncWebServer* p_server, Session* p_session, X509List* p_certList);

    /// @brief Main loop function for the update handling system. This function should be called regularly in the main application loop to process update tasks, handle state transitions, and perform necessary actions based on the current update state. It manages the update state machine and ensures that updates are performed in a controlled manner.
    void loop();

    /// @brief Starts the process of fetching the newest version information for all registered update components. This function initiates the update check by setting the appropriate state and clearing any previous version information. It ensures that the update handling system is ready to fetch and process the latest update data from the specified channels.
    /// @return True if the fetching process was started successfully, false otherwise.
    bool startFetchingNewestVersionInfos();

    /// @brief Enqueues the update tasks for a specific component and its instance index. This function adds the necessary update tasks to the internal queue, allowing the update handling system to process them in the correct order. It ensures that all required steps for updating the specified component are scheduled for execution.
    /// @param componentName The name of the update component for which to enqueue tasks.
    /// @param componentInstanceIndex The index of the component instance for which to enqueue tasks.
    void enqueueUpdateTasks(String componentName, int componentInstanceIndex);

    /// @brief Enqueues a single update task for a specific component, instance index, and update step. This function adds the specified update task to the internal queue, allowing the update handling system to process it in the correct order. It ensures that the specified step for updating the component is scheduled for execution.
    /// @param componentName The name of the update component for which to enqueue the task.
    /// @param instanceIndex The index of the component instance for which to enqueue the task.
    /// @param step The update step to enqueue.
    /// @param handler The handler function for the update step.
    /// @param componentDef Optional pointer to the component instance that created this task.
    /// @return True if the task was enqueued successfully, false otherwise.
    bool enqueueSingleUpdateTask(String componentName, int instanceIndex, UpdateSteps step, update_step_handler_t handler, UpdateHandlingComponentBase* componentDef = nullptr);

    /// @brief Registers a new update component with the UpdateHandling system. This function adds the specified UpdateHandlingComponentBase instance to the internal list of registered components, allowing it to participate in the update process. It ensures that the component is properly initialized and ready for update handling.
    /// @param component Pointer to the UpdateHandlingComponentBase instance to register.
    /// @return True if the component was registered successfully, false otherwise.
    bool registerComponent(UpdateHandlingComponentBase* component);

    /// @brief Returns the number of registered update components. This function provides the total count of components that have been registered with the UpdateHandling system, allowing clients to iterate through the available components and query their status or version information.
    /// @return The number of registered update components.
    size_t getComponentCount() const;

    /// @brief Returns a pointer to the registered update component at the specified index. This function allows clients to access the UpdateHandlingComponentBase instance for a specific component, enabling them to query its status, version information, or perform other operations related to that component.
    /// @param index The index of the component to retrieve.
    /// @return A pointer to the registered update component at the specified index, or nullptr if the index is invalid.
    UpdateHandlingComponentBase* getComponentAt(size_t index) const;

    /// @brief Get the registered update component by its name. This function searches for the UpdateHandlingComponentBase instance with the specified component name and returns a pointer to it.
    /// @param componentName The name of the component to retrieve.
    /// @return A pointer to the registered update component with the specified name, or nullptr if no such component is found.
    UpdateHandlingComponentBase* getComponentByName(String componentName);

    /// @brief Logs a message using the provided logging function. This function formats the log message according to the specified format string and arguments, and passes it to the user-defined logging function for handling. It allows clients to log messages related to the update handling process in a flexible manner.
    /// @param formatP The format string for the log message, stored in program memory (PROGMEM).
    /// @param ... Additional arguments to be formatted into the log message.
    void log(PGM_P formatP, ...);


    log_function_t logFunction;     ///< Function pointer for logging messages during the update process. This member variable allows clients to set a custom logging function to handle log messages generated by the update handling system, enabling flexible logging and debugging capabilities.
    update_status_t updateStatus;   ///< The current update status structure. This member variable holds the state, component, step, and progress information for the update handling system, allowing clients to query the current status of updates.
    Session* p_wifiSession;         ///< Pointer to the session used for managing secure connections. This member variable is used to handle SSL/TLS sessions for secure communication with the update server.
    X509List* p_wifiCertList;       ///< Pointer to the X509List containing the SSL/TLS certificates for secure communication. This member variable is used to manage the list of trusted certificates for verifying the authenticity of the update server during secure connections.

private:

    /// @brief Fetches the version information for all registered update components from the specified update channels. This function performs the necessary HTTP requests to retrieve the manifest files and parse the version information for each component. It updates the internal state with the fetched version data, allowing the update handling system to determine if updates are available.
    /// @return True if the version information was fetched successfully, false otherwise.
    bool fetchVersions();

    /// @brief Clears the version information for all registered update components. This function resets the internal state of the update handling system, marking all version information as invalid and clearing any stored data. It ensures that the update handling system starts with a clean slate before fetching new version information.
    /// @return True if the version information was cleared successfully, false otherwise.
    void clearVersionInfos();

    /// @brief Prepares a JSON document containing the current update status information. This function populates the provided JsonDocument with the relevant data from the update_status_t structure, allowing clients to retrieve the current state of the update handling system via HTTP requests.
    /// @param status The current update status structure containing the state, component, step, and progress information.
    /// @param doc The JsonDocument to be populated with the update status information.
    /// @return True if the document was prepared successfully, false otherwise.
    void prepareStatusDoc(const update_status_t &status, JsonDocument &doc);

    GenericQueue<update_task_t, UPDATE_QUEUE_SIZE> updateTaskQueue; ///< The queue that holds the update tasks to be processed. This member variable is used to manage the sequence of update tasks for different components and steps, allowing the update handling system to process them in an organized manner.
    update_task_t currentUpdateTask;                                ///< The current update task being processed. This member variable holds the details of the update task that is currently being executed by the update handling system.
    const char* stableBaseUrl;                                      ///< The base URL for the stable update channel, where the manifest and update files for stable releases are hosted.
    const char* devBaseUrl;                                         ///< The base URL for the development update channel, where the manifest and update files for development releases are hosted.
    const char* manifestName;                                       ///< The name of the manifest file containing update information. This member variable is used to specify the filename of the manifest that contains the version information and update URLs for the registered components.
    UpdateHandlingComponentBase** updateComponents = nullptr;       ///< Array of pointers to the registered update components. This member variable stores the references to the UpdateHandlingComponentBase instances that have been registered with the UpdateHandling system, allowing for dynamic management of multiple update components.
    size_t updateComponentCount = 0;                                ///< The number of registered update components. This member variable keeps track of the total count of update components that have been registered with the UpdateHandling system, allowing for iteration and management of the components.
};

/*####################################################################*/
/*####################################################################*/

/// @brief Base class for update handling components. This class provides a common interface for different update components to implement their specific update logic and web server endpoints.
class UpdateHandlingComponentBase
{
public:
    /// @brief Constructor for the UpdateHandlingComponentBase class. Initializes the component with the specified component name, and sets the update info and handling pointers to nullptr.
    /// @param name The name of the update component.
    UpdateHandlingComponentBase(const String &name = "") :
        componentName(name), p_updateHandling(nullptr) {}

    /// @brief Virtual destructor for the UpdateHandlingComponentBase class. Ensures proper cleanup of derived classes.
    virtual ~UpdateHandlingComponentBase() {}

    /// @brief Initialize the web server endpoints for this update component. This function should be called during the setup phase of the application to register the necessary HTTP endpoints for handling updates related to this component.
    /// @param p_server Pointer to the AsyncWebServer instance where the endpoints will be registered.
    virtual void initWebserverEndpoints(AsyncWebServer* p_server) = 0;

    /// @brief Enqueue the update tasks specific to this component for the given instance index. This function should be implemented by derived classes to define the sequence of update tasks that need to be performed for this component.
    /// @param componentInstanceIndex The index of the component instance for which the update tasks should be enqueued. If the component has multiple instances, this index specifies which instance to target.
    /// @return Returns true if the update tasks were successfully enqueued, false otherwise.
    virtual bool enqueueUpdateTasks(int componentInstanceIndex = -1) = 0;

    /// @brief Get the number of instances of this update component. This function should be implemented by derived classes to return the total number of instances that the component manages or represents.
    /// @return Returns the number of instances of this component.
    virtual size_t getInstanceCount() = 0;

    /// @brief Query the version of the specified instance of this update component. This function should be implemented by derived classes to return the version information for the given instance index.
    /// @param componentInstanceIndex The index of the component instance for which the version information is requested. If the component has multiple instances, this index specifies which instance to query.
    /// @return A pointer to a string containing the version information, or an empty string if the version cannot be determined.
    virtual char* queryVersion(int componentInstanceIndex) = 0;

    // =================================================
    // Metadata / storage

    String componentName;               ///< The name of the update component. This member variable is used to provide a human-readable identifier for the component and is set during the construction of the derived class.
    update_info_t updateInfo;           ///< Pointer to the update information structure for this component. This member variable is used to store and access the update-related information (such as version, URLs, and MD5 hashes) for this component. It should be initialized by the derived class to point to the appropriate update_info_t instance.
    UpdateHandling* p_updateHandling;   ///< Pointer to the UpdateHandling instance that manages this component. This member variable allows the component to access shared update handling functionality and state. It should be initialized by the derived class to point to the appropriate UpdateHandling instance.
};

#endif