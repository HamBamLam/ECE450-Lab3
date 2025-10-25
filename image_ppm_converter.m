function image_ppm_converter(mode, input_path, output_path)
    if strcmp(mode, 'toPPM')
        img = imread(input_path);
        imwrite(img, output_path, 'ppm');
        fprintf('Saved %s\n', output_path);
        
    elseif strcmp(mode, 'fromPPM')
        img = imread(input_path);
        imshow(img);
        title(['Displaying ', input_path]);
        
    else
        error('Mode must be "toPPM" or "fromPPM".');
    end
end

% image_ppm_converter('fromPPM', 'sobel_output.ppm')
% image_ppm_converter('toPPM', 'image.png', 'output.ppm')