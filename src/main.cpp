const functions = require("firebase-functions");
const admin = require("firebase-admin");

admin.initializeApp();
const db = admin.firestore();

exports.checkSystemDisconnection = functions.pubsub.schedule("every 1 minutes").onRun(async (context) => {
  try {
    // Define the threshold for inactivity
    const oneMinuteInMillis = 60 * 1000;
    const currentTime = Date.now();

    // Get all systems
    const systemsSnapshot = await db.collection("Systems").get();
    const systems = systemsSnapshot.docs;

    for (const systemDoc of systems) {
      const systemData = systemDoc.data();

      // Check if the `lastSeen` field exists
      if (systemData.lastSeen) {
        const lastSeenTime = new Date(systemData.lastSeen).getTime();

        // Compare timestamps to determine if the system is inactive
        if (currentTime - lastSeenTime > oneMinuteInMillis) {
          const notificationMessage = `${systemData.systemName || "Unknown System"} is disconnected.`;

          // Add a notification document
          await db.collection("Notifications").add({
            systemName: systemData.systemName || "Unknown System",
            message: notificationMessage,
            timestamp: admin.firestore.FieldValue.serverTimestamp(),
          });

          console.log(`Notification created: ${notificationMessage}`);
        }
      } else {
        console.log(`No lastSeen field for system: ${systemDoc.id}`);
      }
    }

    return null;
  } catch (error) {
    console.error("Error checking system disconnections:", error);
    throw new functions.https.HttpsError("internal", "Unable to check system disconnections.");
  }
});
