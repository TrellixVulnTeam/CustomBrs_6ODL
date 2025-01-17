description("Series of tests to ensure correct behaviour of canvas.currentTransform");
var ctx = document.createElement('canvas').getContext('2d');

var matrix = ctx.currentTransform;

debug("Check initial currentTransform values");
shouldBe("matrix.a", "1");
shouldBe("matrix.b", "0");
shouldBe("matrix.c", "0");
shouldBe("matrix.d", "1");
shouldBe("matrix.e", "0");
shouldBe("matrix.f", "0");

function setCurrentTransform(ctx, a, b, c, d, e, f)
{
    matrix.a = a;
    matrix.b = b;
    matrix.c = c;
    matrix.d = d;
    matrix.e = e;
    matrix.f = f;
    ctx.currentTransform = matrix;
    matrix.a = NaN;
    matrix.b = NaN;
    matrix.c = NaN;
    matrix.d = NaN;
    matrix.e = NaN;
    matrix.f = NaN;
    matrix = ctx.currentTransform;
    shouldBe("matrix.a", "" + a);
    shouldBe("matrix.b", "" + b);
    shouldBe("matrix.c", "" + c);
    shouldBe("matrix.d", "" + d);
    shouldBe("matrix.e", "" + e);
    shouldBe("matrix.f", "" + f);
}

matrix.a = 2;
debug("Changing matrix should not affect the CTM");
shouldBe("ctx.currentTransform.a", "1");
shouldBe("ctx.currentTransform.b", "0");
shouldBe("ctx.currentTransform.c", "0");
shouldBe("ctx.currentTransform.d", "1");
shouldBe("ctx.currentTransform.e", "0");
shouldBe("ctx.currentTransform.f", "0");

debug("Reset the CTM to the initial matrix");
ctx.beginPath();
ctx.scale(0.5, 0.5);
matrix = ctx.currentTransform;
shouldBe("matrix.a", "0.5");
shouldBe("matrix.b", "0");
shouldBe("matrix.c", "0");
shouldBe("matrix.d", "0.5");
shouldBe("matrix.e", "0");
shouldBe("matrix.f", "0");
setCurrentTransform(ctx, 1, 0, 0, 1, 0, 0);
ctx.fillStyle = 'green';
ctx.fillRect(0, 0, 100, 100);

var imageData = ctx.getImageData(1, 1, 98, 98);
var imgdata = imageData.data;
shouldBe("imgdata[4]", "0");
shouldBe("imgdata[5]", "128");
shouldBe("imgdata[6]", "0");

debug("currentTransform should not affect the current path");
ctx.beginPath();
ctx.rect(0,0,100,100);
ctx.save();
setCurrentTransform(ctx, 0.5, 0, 0, 0.5, 10, 10);
ctx.fillStyle = 'red';
ctx.fillRect(0, 0, 100, 100);
ctx.restore();
matrix = ctx.currentTransform;
shouldBe("matrix.a", "1");
shouldBe("matrix.b", "0");
shouldBe("matrix.c", "0");
shouldBe("matrix.d", "1");
shouldBe("matrix.e", "0");
shouldBe("matrix.f", "0");
ctx.fillStyle = 'green';
ctx.fillRect(0, 0, 100, 100);

imageData = ctx.getImageData(1, 1, 98, 98);
imgdata = imageData.data;
shouldBe("imgdata[4]", "0");
shouldBe("imgdata[5]", "128");
shouldBe("imgdata[6]", "0");

debug("currentTransform should not affect the CTM outside of save() and restore()");
ctx.beginPath();
ctx.fillStyle = 'green';
ctx.save();
setCurrentTransform(ctx, 0.5, 0, 0, 0.5, 0, 0);
ctx.fillStyle = 'red';
ctx.fillRect(0, 0, 100, 100);
ctx.restore();
matrix = ctx.currentTransform;
shouldBe("matrix.a", "1");
shouldBe("matrix.b", "0");
shouldBe("matrix.c", "0");
shouldBe("matrix.d", "1");
shouldBe("matrix.e", "0");
shouldBe("matrix.f", "0");
ctx.fillRect(0, 0, 100, 100);

imageData = ctx.getImageData(1, 1, 98, 98);
imgdata = imageData.data;
shouldBe("imgdata[4]", "0");
shouldBe("imgdata[5]", "128");
shouldBe("imgdata[6]", "0");

debug("stop drawing on not-invertible CTM");
ctx.beginPath();
ctx.fillStyle = 'green';
ctx.fillRect(0, 0, 100, 100);
setCurrentTransform(ctx, 0, 0, 0, 0, 0, 0);
ctx.fillStyle = 'red';
ctx.fillRect(0, 0, 100, 100);

imageData = ctx.getImageData(1, 1, 98, 98);
imgdata = imageData.data;
shouldBe("imgdata[4]", "0");
shouldBe("imgdata[5]", "128");
shouldBe("imgdata[6]", "0");

debug("currentTransform with a not-invertible matrix should only stop the drawing up to the next restore()");
ctx.beginPath();
ctx.resetTransform();
matrix = ctx.currentTransform;
shouldBe("matrix.a", "1");
shouldBe("matrix.b", "0");
shouldBe("matrix.c", "0");
shouldBe("matrix.d", "1");
shouldBe("matrix.e", "0");
shouldBe("matrix.f", "0");
ctx.save();
setCurrentTransform(ctx, 0, 0, 0, 0, 0, 0);
ctx.fillStyle = 'red';
ctx.fillRect(0, 0, 100, 100);
ctx.restore();
matrix = ctx.currentTransform;
shouldBe("matrix.a", "1");
shouldBe("matrix.b", "0");
shouldBe("matrix.c", "0");
shouldBe("matrix.d", "1");
shouldBe("matrix.e", "0");
shouldBe("matrix.f", "0");
ctx.fillStyle = 'blue';
ctx.fillRect(0, 0, 100, 100);

imageData = ctx.getImageData(1, 1, 98, 98);
imgdata = imageData.data;
shouldBe("imgdata[4]", "0");
shouldBe("imgdata[5]", "0");
shouldBe("imgdata[6]", "255");

debug("currentTransform should set transform although CTM is not-invertible");
ctx.beginPath();
ctx.fillStyle = 'red';
ctx.fillRect(0, 0, 100, 100);
setCurrentTransform(ctx, 0, 0, 0, 0, 0, 0);
ctx.fillStyle = 'green';
ctx.fillRect(0, 0, 100, 100);
setCurrentTransform(ctx, 1, 0, 0, 1, 0, 0);
ctx.fillStyle = 'blue';
ctx.fillRect(0, 0, 100, 100);

imageData = ctx.getImageData(1, 1, 98, 98);
imgdata = imageData.data;
shouldBe("imgdata[4]", "0");
shouldBe("imgdata[5]", "0");
shouldBe("imgdata[6]", "255");

debug("Check assigning an invalid object throws exception as expected");
shouldThrow("ctx.currentTransform = ctx", '"TypeError: Failed to set the \'currentTransform\' property on \'CanvasRenderingContext2D\': The provided value is not of type \'SVGMatrix\'."');

debug("Check handling non-finite values. see 2d.transformation.setTransform.nonfinite.html");
ctx.fillStyle = 'red';
ctx.fillRect(0, 0, 100, 100);

function setCurrentTransformToNonfinite(ctx, a, b, c, d, e, f)
{
    matrix.a = a;
    matrix.b = b;
    matrix.c = c;
    matrix.d = d;
    matrix.e = e;
    matrix.f = f;
    ctx.currentTransform = matrix;
    matrix.a = NaN;
    matrix.b = NaN;
    matrix.c = NaN;
    matrix.d = NaN;
    matrix.e = NaN;
    matrix.f = NaN;
    matrix = ctx.currentTransform;
    shouldBe("matrix.a", "1");
    shouldBe("matrix.b", "0");
    shouldBe("matrix.c", "0");
    shouldBe("matrix.d", "1");
    shouldBe("matrix.e", "100");
    shouldBe("matrix.f", "10");
}

ctx.translate(100, 10);
matrix = ctx.currentTransform;
shouldBe("matrix.a", "1");
shouldBe("matrix.b", "0");
shouldBe("matrix.c", "0");
shouldBe("matrix.d", "1");
shouldBe("matrix.e", "100");
shouldBe("matrix.f", "10");
setCurrentTransformToNonfinite(ctx, Infinity, 0, 0, 0, 0, 0);
setCurrentTransformToNonfinite(ctx, -Infinity, 0, 0, 0, 0, 0);
setCurrentTransformToNonfinite(ctx, NaN, 0, 0, 0, 0, 0);
setCurrentTransformToNonfinite(ctx, 0, Infinity, 0, 0, 0, 0);
setCurrentTransformToNonfinite(ctx, 0, -Infinity, 0, 0, 0, 0);
setCurrentTransformToNonfinite(ctx, 0, NaN, 0, 0, 0, 0);
setCurrentTransformToNonfinite(ctx, 0, 0, Infinity, 0, 0, 0);
setCurrentTransformToNonfinite(ctx, 0, 0, -Infinity, 0, 0, 0);
setCurrentTransformToNonfinite(ctx, 0, 0, NaN, 0, 0, 0);
setCurrentTransformToNonfinite(ctx, 0, 0, 0, Infinity, 0, 0);
setCurrentTransformToNonfinite(ctx, 0, 0, 0, -Infinity, 0, 0);
setCurrentTransformToNonfinite(ctx, 0, 0, 0, NaN, 0, 0);
setCurrentTransformToNonfinite(ctx, 0, 0, 0, 0, Infinity, 0);
setCurrentTransformToNonfinite(ctx, 0, 0, 0, 0, -Infinity, 0);
setCurrentTransformToNonfinite(ctx, 0, 0, 0, 0, NaN, 0);
setCurrentTransformToNonfinite(ctx, 0, 0, 0, 0, 0, Infinity);
setCurrentTransformToNonfinite(ctx, 0, 0, 0, 0, 0, -Infinity);
setCurrentTransformToNonfinite(ctx, 0, 0, 0, 0, 0, NaN);
setCurrentTransformToNonfinite(ctx, Infinity, Infinity, 0, 0, 0, 0);
setCurrentTransformToNonfinite(ctx, Infinity, Infinity, Infinity, 0, 0, 0);
setCurrentTransformToNonfinite(ctx, Infinity, Infinity, Infinity, Infinity, 0, 0);
setCurrentTransformToNonfinite(ctx, Infinity, Infinity, Infinity, Infinity, Infinity, 0);
setCurrentTransformToNonfinite(ctx, Infinity, Infinity, Infinity, Infinity, Infinity, Infinity);
setCurrentTransformToNonfinite(ctx, Infinity, Infinity, Infinity, Infinity, 0, Infinity);
setCurrentTransformToNonfinite(ctx, Infinity, Infinity, Infinity, 0, Infinity, 0);
setCurrentTransformToNonfinite(ctx, Infinity, Infinity, Infinity, 0, Infinity, Infinity);
setCurrentTransformToNonfinite(ctx, Infinity, Infinity, Infinity, 0, 0, Infinity);
setCurrentTransformToNonfinite(ctx, Infinity, Infinity, 0, Infinity, 0, 0);
setCurrentTransformToNonfinite(ctx, Infinity, Infinity, 0, Infinity, Infinity, 0);
setCurrentTransformToNonfinite(ctx, Infinity, Infinity, 0, Infinity, Infinity, Infinity);
setCurrentTransformToNonfinite(ctx, Infinity, Infinity, 0, Infinity, 0, Infinity);
setCurrentTransformToNonfinite(ctx, Infinity, Infinity, 0, 0, Infinity, 0);
setCurrentTransformToNonfinite(ctx, Infinity, Infinity, 0, 0, Infinity, Infinity);
setCurrentTransformToNonfinite(ctx, Infinity, Infinity, 0, 0, 0, Infinity);
setCurrentTransformToNonfinite(ctx, Infinity, 0, Infinity, 0, 0, 0);
setCurrentTransformToNonfinite(ctx, Infinity, 0, Infinity, Infinity, 0, 0);
setCurrentTransformToNonfinite(ctx, Infinity, 0, Infinity, Infinity, Infinity, 0);
setCurrentTransformToNonfinite(ctx, Infinity, 0, Infinity, Infinity, Infinity, Infinity);
setCurrentTransformToNonfinite(ctx, Infinity, 0, Infinity, Infinity, 0, Infinity);
setCurrentTransformToNonfinite(ctx, Infinity, 0, Infinity, 0, Infinity, 0);
setCurrentTransformToNonfinite(ctx, Infinity, 0, Infinity, 0, Infinity, Infinity);
setCurrentTransformToNonfinite(ctx, Infinity, 0, Infinity, 0, 0, Infinity);
setCurrentTransformToNonfinite(ctx, Infinity, 0, 0, Infinity, 0, 0);
setCurrentTransformToNonfinite(ctx, Infinity, 0, 0, Infinity, Infinity, 0);
setCurrentTransformToNonfinite(ctx, Infinity, 0, 0, Infinity, Infinity, Infinity);
setCurrentTransformToNonfinite(ctx, Infinity, 0, 0, Infinity, 0, Infinity);
setCurrentTransformToNonfinite(ctx, Infinity, 0, 0, 0, Infinity, 0);
setCurrentTransformToNonfinite(ctx, Infinity, 0, 0, 0, Infinity, Infinity);
setCurrentTransformToNonfinite(ctx, Infinity, 0, 0, 0, 0, Infinity);
setCurrentTransformToNonfinite(ctx, 0, Infinity, Infinity, 0, 0, 0);
setCurrentTransformToNonfinite(ctx, 0, Infinity, Infinity, Infinity, 0, 0);
setCurrentTransformToNonfinite(ctx, 0, Infinity, Infinity, Infinity, Infinity, 0);
setCurrentTransformToNonfinite(ctx, 0, Infinity, Infinity, Infinity, Infinity, Infinity);
setCurrentTransformToNonfinite(ctx, 0, Infinity, Infinity, Infinity, 0, Infinity);
setCurrentTransformToNonfinite(ctx, 0, Infinity, Infinity, 0, Infinity, 0);
setCurrentTransformToNonfinite(ctx, 0, Infinity, Infinity, 0, Infinity, Infinity);
setCurrentTransformToNonfinite(ctx, 0, Infinity, Infinity, 0, 0, Infinity);
setCurrentTransformToNonfinite(ctx, 0, Infinity, 0, Infinity, 0, 0);
setCurrentTransformToNonfinite(ctx, 0, Infinity, 0, Infinity, Infinity, 0);
setCurrentTransformToNonfinite(ctx, 0, Infinity, 0, Infinity, Infinity, Infinity);
setCurrentTransformToNonfinite(ctx, 0, Infinity, 0, Infinity, 0, Infinity);
setCurrentTransformToNonfinite(ctx, 0, Infinity, 0, 0, Infinity, 0);
setCurrentTransformToNonfinite(ctx, 0, Infinity, 0, 0, Infinity, Infinity);
setCurrentTransformToNonfinite(ctx, 0, Infinity, 0, 0, 0, Infinity);
setCurrentTransformToNonfinite(ctx, 0, 0, Infinity, Infinity, 0, 0);
setCurrentTransformToNonfinite(ctx, 0, 0, Infinity, Infinity, Infinity, 0);
setCurrentTransformToNonfinite(ctx, 0, 0, Infinity, Infinity, Infinity, Infinity);
setCurrentTransformToNonfinite(ctx, 0, 0, Infinity, Infinity, 0, Infinity);
setCurrentTransformToNonfinite(ctx, 0, 0, Infinity, 0, Infinity, 0);
setCurrentTransformToNonfinite(ctx, 0, 0, Infinity, 0, Infinity, Infinity);
setCurrentTransformToNonfinite(ctx, 0, 0, Infinity, 0, 0, Infinity);
setCurrentTransformToNonfinite(ctx, 0, 0, 0, Infinity, Infinity, 0);
setCurrentTransformToNonfinite(ctx, 0, 0, 0, Infinity, Infinity, Infinity);
setCurrentTransformToNonfinite(ctx, 0, 0, 0, Infinity, 0, Infinity);
setCurrentTransformToNonfinite(ctx, 0, 0, 0, 0, Infinity, Infinity);
matrix = ctx.currentTransform;
shouldBe("matrix.a", "1");
shouldBe("matrix.b", "0");
shouldBe("matrix.c", "0");
shouldBe("matrix.d", "1");
shouldBe("matrix.e", "100");
shouldBe("matrix.f", "10");

ctx.fillStyle = 'green';
ctx.fillRect(-100, -10, 100, 100);

imageData = ctx.getImageData(1, 1, 98, 98);
imgdata = imageData.data;
shouldBe("imgdata[4]", "0");
shouldBe("imgdata[5]", "128");
shouldBe("imgdata[6]", "0");
